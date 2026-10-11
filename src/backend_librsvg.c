#include "backend.h"
#include "image.h"
#include "source.h"
#include "source_private.h"

#include <librsvg/rsvg.h>
#include <stdlib.h>
#include <string.h>

/* Some systems like GNU/Hurd don't define PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct private {
  void *data;
  size_t len;
  char *path;
};

static void free_private(void *raw_private)
{
  struct private *private = raw_private;
  free(private->path);
  free(raw_private);
}

static void load_image(void *raw_private, struct imv_image **image, int *frametime)
{
  *image = NULL;
  *frametime = 0;

  struct private *private = raw_private;

  RsvgHandle *handle = NULL;
  GError *error = NULL;

  if (private->data) {
    handle = rsvg_handle_new_from_data(private->data, private->len, &error);
  } else {
    handle = rsvg_handle_new_from_file(private->path, &error);
  }

  if (handle) {
    *image = imv_image_create_from_svg(handle);
  }
  g_clear_error(&error);
}

static const struct imv_source_vtable vtable = {
  .load_first_frame = load_image,
  .free = free_private
};

static enum backend_result open_path(const char *path, struct imv_source **src)
{
  /* Look for an <SVG> tag near the start of the file */
  char header[4096];
  FILE *f = fopen(path, "rb");
  if (!f) {
    return BACKEND_BAD_PATH;
  }
  size_t len = fread(header, 1, sizeof header - 1, f);
  fclose(f);

  header[len] = 0;
  if (!strstr(header, "<SVG") && !strstr(header, "<svg")) {
    return BACKEND_UNSUPPORTED;
  }

  struct private *private = malloc(sizeof *private);
  private->data = NULL;
  private->len = 0;
  private->path = strdup(path);

  *src = imv_source_create(&vtable, private);
  return BACKEND_SUCCESS;
}

static enum backend_result open_memory(void *data, size_t len, struct imv_source **src)
{
  /* Look for an <SVG> tag near the start of the file */
  char header[4096];
  size_t header_len = sizeof header - 1;
  if (header_len > len) {
    header_len = len;
  }
  memcpy(header, data, header_len);
  header[header_len] = 0;
  if (!strstr(header, "<SVG") && !strstr(header, "<svg")) {
    return BACKEND_UNSUPPORTED;
  }

  struct private *private = malloc(sizeof *private);
  private->data = data;
  private->len = len;
  private->path = NULL;

  *src = imv_source_create(&vtable, private);
  return BACKEND_SUCCESS;
}

const struct imv_backend imv_backend_librsvg = {
  .name = "libRSVG",
  .description = "SVG library developed by GNOME",
  .website = "https://wiki.gnome.org/Projects/LibRsvg",
  .license = "GNU Lesser General Public License v2.1+",
  .open_path = &open_path,
  .open_memory = &open_memory,
};
