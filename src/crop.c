#include "crop.h"
#include "image.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <math.h>
#include <png.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool imv_crop_geometry(const struct imv_image *image, int x, int y,
    double scale, double rotation, bool mirrored, struct imv_crop_geometry *out)
{
  const int w = imv_image_width(image), h = imv_image_height(image);
  if (w <= 0 || h <= 0 || !isfinite(scale) || scale <= 0 || !isfinite(rotation)) {
    return false;
  }
  double angle = fmod(rotation, 360) * G_PI / 180;
  double c = cos(angle), s = sin(angle);
  if (fabs(c) < 1e-10) c = 0;
  if (fabs(s) < 1e-10) s = 0;
  double flip = mirrored ? -1 : 1;
  cairo_matrix_init(&out->matrix, flip * c, s, -flip * s, c, 0, 0);
  double left = 0, top = 0, right = 0, bottom = 0;
  for (int i = 0; i < 4; ++i) {
    double px = i & 1 ? w : 0, py = i & 2 ? h : 0;
    cairo_matrix_transform_point(&out->matrix, &px, &py);
    left = MIN(left, px);
    right = MAX(right, px);
    top = MIN(top, py);
    bottom = MAX(bottom, py);
  }
  double width = ceil(right - left - 1e-9), height = ceil(bottom - top - 1e-9);
  if (width > INT_MAX || height > INT_MAX) {
    return false;
  }

  /* Match the canvas's rotation about the displayed image centre. */
  double cx = x + w * scale / 2, cy = y + h * scale / 2;
  if (imv_image_get_bitmap(image)) {
    cx = trunc(cx);
    cy = trunc(cy);
  }
  double dx = x - cx, dy = y - cy;
  cairo_matrix_transform_distance(&out->matrix, &dx, &dy);
  out->x = cx + dx + left * scale;
  out->y = cy + dy + top * scale;
  out->scale = scale;
  out->width = width;
  out->height = height;
  out->matrix.x0 = -left;
  out->matrix.y0 = -top;
  return true;
}

bool imv_crop_select(const struct imv_crop_geometry *geometry,
    double x1, double y1, double x2, double y2, struct imv_crop_rect *out)
{
  *out = (struct imv_crop_rect){0};
  if (!isfinite(x1) || !isfinite(y1) || !isfinite(x2) || !isfinite(y2) ||
      !isfinite(geometry->scale) || geometry->scale <= 0) {
    return false;
  }
  double left = CLAMP((MIN(x1, x2) - geometry->x) / geometry->scale, 0, geometry->width);
  double right = CLAMP((MAX(x1, x2) - geometry->x) / geometry->scale, 0, geometry->width);
  double top = CLAMP((MIN(y1, y2) - geometry->y) / geometry->scale, 0, geometry->height);
  double bottom = CLAMP((MAX(y1, y2) - geometry->y) / geometry->scale, 0, geometry->height);
  if (right - left < 1e-7 || bottom - top < 1e-7) {
    return false;
  }
  out->x = floor(left + 1e-7);
  out->y = floor(top + 1e-7);
  out->width = ceil(right - 1e-7) - out->x;
  out->height = ceil(bottom - 1e-7) - out->y;
  return out->width > 0 && out->height > 0;
}

struct imv_bitmap imv_crop_extract(const struct imv_image *image,
    const struct imv_crop_geometry *geometry, const struct imv_crop_rect *rect,
    GError **error)
{
  struct imv_bitmap result = {0};
  if (!image || rect->x < 0 || rect->y < 0 || rect->width <= 0 || rect->height <= 0 ||
      rect->width > geometry->width || rect->height > geometry->height ||
      rect->x > geometry->width - rect->width ||
      rect->y > geometry->height - rect->height) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
        "Select a non-empty area inside the image");
    return result;
  }
  result = imv_bitmap_alloc(rect->width, rect->height);
  if (!result.data) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Not enough memory for crop");
    return result;
  }
  const struct imv_bitmap *source = imv_image_get_bitmap(image);
  if (source) {
    cairo_matrix_t inverse = geometry->matrix;
    if (!source->data || cairo_matrix_invert(&inverse) != CAIRO_STATUS_SUCCESS) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid image transform");
      goto fail;
    }
    for (int y = 0; y < result.height; ++y) {
      for (int x = 0; x < result.width; ++x) {
        double sx = rect->x + x + 0.5, sy = rect->y + y + 0.5;
        cairo_matrix_transform_point(&inverse, &sx, &sy);
        uint8_t *pixel = result.data + ((size_t)y * result.width + x) * 4;
        if (sx >= 0 && sy >= 0 && sx < source->width && sy < source->height) {
          memcpy(pixel, source->data + ((size_t)(int)sy * source->width + (int)sx) * 4, 4);
        } else {
          memset(pixel, 0, 4);
        }
      }
    }
    return result;
  }
#ifdef IMV_BACKEND_LIBRSVG
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
      result.width, result.height);
  cairo_t *cr = cairo_create(surface);
  cairo_translate(cr, -rect->x, -rect->y);
  cairo_transform(cr, &geometry->matrix);
  bool rendered = rsvg_handle_render_cairo(imv_image_get_svg(image), cr);
  cairo_status_t status = cairo_status(cr);
  cairo_destroy(cr);
  if (rendered && status == CAIRO_STATUS_SUCCESS) {
    cairo_surface_flush(surface);
    const unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < result.height; ++y) {
      const uint32_t *row = (const uint32_t *)(data + (size_t)y * stride);
      for (int x = 0; x < result.width; ++x) {
        unsigned a = row[x] >> 24;
        uint8_t *pixel = result.data + ((size_t)y * result.width + x) * 4;
        pixel[0] = a ? (((row[x] >> 16) & 255) * 255 + a / 2) / a : 0;
        pixel[1] = a ? (((row[x] >> 8) & 255) * 255 + a / 2) / a : 0;
        pixel[2] = a ? ((row[x] & 255) * 255 + a / 2) / a : 0;
        pixel[3] = a;
      }
    }
  }
  cairo_surface_destroy(surface);
  if (rendered && status == CAIRO_STATUS_SUCCESS) {
    return result;
  }
#endif
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not render image for crop");
fail:
  imv_bitmap_free(result);
  return (struct imv_bitmap){0};
}

char *imv_crop_save(const struct imv_image *image,
    const struct imv_crop_geometry *geometry, const struct imv_crop_rect *rect,
    const char *original, GError **error)
{
  struct imv_bitmap bitmap = imv_crop_extract(image, geometry, rect, error);
  if (!bitmap.data) {
    return NULL;
  }
  char *stem = g_strdup(original && *original && strcmp(original, "-") ? original : "image");
  char *basename = strrchr(stem, '/');
  basename = basename ? basename + 1 : stem;
  char *extension = strrchr(basename, '.');
  if (extension && extension != basename) {
    *extension = '\0';
  }
  char *path = NULL;
  int fd = -1;
  for (unsigned number = 1; number < UINT_MAX; ++number) {
    g_free(path);
    path = number == 1 ? g_strdup_printf("%s-crop.png", stem) :
        g_strdup_printf("%s-crop-%u.png", stem, number);
    fd = g_open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd >= 0 || errno != EEXIST) {
      break;
    }
  }
  g_free(stem);
  if (fd < 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
        "Cannot create crop: %s", g_strerror(errno));
    goto fail;
  }
  FILE *file = fdopen(fd, "wb");
  if (!file) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
        "Cannot open crop: %s", g_strerror(errno));
    close(fd);
    g_unlink(path);
    goto fail;
  }
  png_image png = {.version = PNG_IMAGE_VERSION, .width = bitmap.width,
      .height = bitmap.height, .format = PNG_FORMAT_RGBA};
  bool written = png_image_write_to_stdio(&png, file, 0, bitmap.data, 0, NULL);
  if (!written) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot write PNG: %s", png.message);
  }
  png_image_free(&png);
  if (fclose(file) != 0 && written) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
        "Cannot finish writing PNG: %s", g_strerror(errno));
    written = false;
  }
  if (!written) {
    g_unlink(path);
    goto fail;
  }
  imv_bitmap_free(bitmap);
  return path;
fail:
  imv_bitmap_free(bitmap);
  g_free(path);
  return NULL;
}
