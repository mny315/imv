#ifndef IMV_CROP_H
#define IMV_CROP_H

#include <stdbool.h>
#include <cairo.h>
#include <glib.h>
#include "bitmap.h"

struct imv_image;

struct imv_crop_geometry {
  /* Original pixels to the rotated/mirrored image's bounding box. */
  cairo_matrix_t matrix;
  double x, y, scale;
  int width, height;
};

struct imv_crop_rect {
  int x, y, width, height;
};

bool imv_crop_geometry(const struct imv_image *image, int x, int y,
    double scale, double rotation, bool mirrored, struct imv_crop_geometry *out);

/* Convert a drag in framebuffer coordinates to a clamped pixel rectangle. */
bool imv_crop_select(const struct imv_crop_geometry *geometry,
    double x1, double y1, double x2, double y2, struct imv_crop_rect *out);

/* Extract native-resolution RGBA pixels; the caller owns the result. */
struct imv_bitmap imv_crop_extract(const struct imv_image *image,
    const struct imv_crop_geometry *geometry, const struct imv_crop_rect *rect,
    GError **error);

/* Save beside original as NAME-crop[-N].png, exclusively creating a new file.
 * NULL or "-" as original saves image-crop[-N].png in the working directory.
 * Returns the new path (g_free by caller), or NULL on failure. */
char *imv_crop_save(const struct imv_image *image,
    const struct imv_crop_geometry *geometry, const struct imv_crop_rect *rect,
    const char *original, GError **error);

#endif
