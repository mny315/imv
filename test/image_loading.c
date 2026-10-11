#include "backend.h"
#include "bitmap.h"
#include "image.h"
#include "source.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static void decode(const struct imv_backend *backend, void *data, size_t len,
    bool valid, bool still)
{
  struct imv_source *source = NULL;
  enum backend_result result = backend->open_memory(data, len, &source);
  if (valid) g_assert_cmpint(result, ==, BACKEND_SUCCESS);
  if (result == BACKEND_SUCCESS) {
    struct imv_image *image = NULL;
    int frametime = -1;
    g_assert_true(imv_source_load_first_frame(source, &image, &frametime));
    if (valid) {
      g_assert_nonnull(image);
      g_assert_cmpint(imv_image_width(image), >, 0);
      g_assert_cmpint(imv_image_height(image), >, 0);
      if (still) g_assert_cmpint(frametime, ==, 0);
    }
    imv_image_free(image);
    imv_source_free(source);
  }
}

static void check_sample(const struct imv_backend *backend, const char *name)
{
  char *path = g_build_filename(IMV_TEST_DATA, name, NULL);
  char *data;
  size_t len;
  g_assert_true(g_file_get_contents(path, &data, &len, NULL));
  decode(backend, data, len, true, true);
  for (size_t truncated = 0; truncated < MIN(len, 96); truncated += 7) {
    decode(backend, data, truncated, false, false);
  }
  g_free(data);
  g_free(path);
}

#ifdef IMV_BACKEND_FARBFELD
extern const struct imv_backend imv_backend_farbfeld;
static void test_farbfeld(void)
{
  unsigned char bytes[24] = "farbfeld";
  bytes[11] = bytes[15] = 1;
  bytes[16] = bytes[18] = bytes[20] = bytes[22] = 255;
  decode(&imv_backend_farbfeld, bytes, sizeof bytes, true, true);
  bytes[11] = 0;
  struct imv_source *source;
  g_assert_cmpint(imv_backend_farbfeld.open_memory(bytes, 16, &source), ==, BACKEND_UNSUPPORTED);
  memset(bytes + 8, 255, 8);
  g_assert_cmpint(imv_backend_farbfeld.open_memory(bytes, 16, &source), ==, BACKEND_UNSUPPORTED);
  bytes[8] = bytes[12] = 0x7f;
  g_assert_cmpint(imv_backend_farbfeld.open_memory(bytes, 16, &source), ==, BACKEND_UNSUPPORTED);
}
#endif

#ifdef IMV_BACKEND_LIBPNG
extern const struct imv_backend imv_backend_libpng;
static void test_png(void) { check_sample(&imv_backend_libpng, "sample.png"); }
#endif
#ifdef IMV_BACKEND_LIBTIFF
extern const struct imv_backend imv_backend_libtiff;
static void test_tiff(void) { check_sample(&imv_backend_libtiff, "sample.tiff"); }
#endif
#ifdef IMV_BACKEND_LIBJPEG
extern const struct imv_backend imv_backend_libjpeg;
static void test_jpeg(void) { check_sample(&imv_backend_libjpeg, "sample.jpeg"); }
#endif
#ifdef IMV_BACKEND_LIBWEBP
extern const struct imv_backend imv_backend_libwebp;
static void test_webp(void) { check_sample(&imv_backend_libwebp, "sample.webp"); }
static void test_webp_loop(void)
{
  /* Generated lossless 1x1 red/green frames with durations 100/200 ms. */
  size_t len;
  unsigned char *bytes = g_base64_decode("UklGRoQAAABXRUJQVlA4WAoAAAACAAAAAAAAAAAAQU5JTQYAAAD/////AABBTk1GKAAAAAAAAAAAAAAAAAAAAGQAAAJWUDhMDwAAAC8AAAAABxD9j/4HIqL/AQBBTk1GKAAAAAAAAAAAAAAAAAAAAMgAAABWUDhMDwAAAC8AAAAAB9D/iP4HIqL/AQA=", &len);
  struct imv_source *source;
  g_assert_cmpint(imv_backend_libwebp.open_memory(bytes, len, &source), ==, BACKEND_SUCCESS);
  for (int i = 0; i < 6; ++i) {
    struct imv_image *image = NULL;
    int duration;
    /* Both frame entry points use the same sequential decoder. */
    g_assert_true(imv_source_load_first_frame(source, &image, &duration));
    g_assert_nonnull(image);
    g_assert_cmpint(duration, ==, i % 2 ? 200 : 100);
    const struct imv_bitmap *bitmap = imv_image_get_bitmap(image);
    g_assert_cmpint(bitmap->data[i % 2], ==, 255);
    imv_image_free(image);
  }
  imv_source_free(source);
  g_free(bytes);
}

#endif
#ifdef IMV_BACKEND_LIBRSVG
extern const struct imv_backend imv_backend_librsvg;
static void test_svg(void)
{
  char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='3' height='2'><rect width='3' height='2'/></svg>";
  decode(&imv_backend_librsvg, svg, sizeof svg - 1, true, true);
  struct imv_source *source;
  g_assert_cmpint(imv_backend_librsvg.open_memory(svg, 0, &source), ==, BACKEND_UNSUPPORTED);
  /* A path is a filename, not a file:// URI; # and % must remain literal. */
  char *root = g_dir_make_tmp("imv-svg-audit-XXXXXX", NULL);
  char *path = g_build_filename(root, "with space #%20.svg", NULL);
  g_assert_true(g_file_set_contents(path, svg, -1, NULL));
  g_assert_cmpint(imv_backend_librsvg.open_path(path, &source), ==, BACKEND_SUCCESS);
  struct imv_image *image = NULL;
  int duration;
  g_assert_true(imv_source_load_first_frame(source, &image, &duration));
  g_assert_nonnull(image);
  g_assert_cmpint(imv_image_width(image), ==, 3);
  imv_image_free(image);
  imv_source_free(source);
  g_unlink(path); g_rmdir(root); g_free(path); g_free(root);
}
#endif

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
#ifdef IMV_BACKEND_FARBFELD
  g_test_add_func("/regression/backends/farbfeld", test_farbfeld);
#endif
#ifdef IMV_BACKEND_LIBPNG
  g_test_add_func("/regression/backends/png", test_png);
#endif
#ifdef IMV_BACKEND_LIBTIFF
  g_test_add_func("/regression/backends/tiff", test_tiff);
#endif
#ifdef IMV_BACKEND_LIBJPEG
  g_test_add_func("/regression/backends/jpeg", test_jpeg);
#endif
#ifdef IMV_BACKEND_LIBWEBP
  g_test_add_func("/regression/backends/webp", test_webp);
  g_test_add_func("/regression/backends/webp-loop", test_webp_loop);
#endif
#ifdef IMV_BACKEND_LIBRSVG
  g_test_add_func("/regression/backends/svg", test_svg);
#endif
  return g_test_run();
}
