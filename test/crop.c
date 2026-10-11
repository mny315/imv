#include "crop.h"
#include "image.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <math.h>
#include <png.h>
#include <unistd.h>

static struct imv_image *sample(void)
{
  struct imv_bitmap bitmap = imv_bitmap_alloc(3, 2);
  for (int i = 0; i < 6; ++i) {
    bitmap.data[i * 4] = i + 1;
    bitmap.data[i * 4 + 1] = 50 + i;
    bitmap.data[i * 4 + 2] = 100 + i;
    bitmap.data[i * 4 + 3] = i * 51;
  }
  return imv_image_create_from_bitmap(bitmap);
}

static void test_selection(void)
{
  struct imv_image *image = sample();
  struct imv_crop_geometry g;
  struct imv_crop_rect r;
  g_assert_true(imv_crop_geometry(image, 100, 200, 2, 0, false, &g));
  g_assert_true(imv_crop_select(&g, 102, 202, 106, 204, &r));
  g_assert_cmpint(r.x, ==, 1);
  g_assert_cmpint(r.y, ==, 1);
  g_assert_cmpint(r.width, ==, 2);
  g_assert_cmpint(r.height, ==, 1);
  struct imv_crop_rect reverse;
  g_assert_true(imv_crop_select(&g, 106, 204, 102, 202, &reverse));
  g_assert_cmpmem(&r, sizeof r, &reverse, sizeof reverse);
  g_assert_true(imv_crop_select(&g, 0, 0, 1000, 1000, &r));
  g_assert_cmpint(r.x, ==, 0);
  g_assert_cmpint(r.y, ==, 0);
  g_assert_cmpint(r.width, ==, 3);
  g_assert_cmpint(r.height, ==, 2);
  g_assert_false(imv_crop_select(&g, 100, 200, 100, 204, &r));
  g_assert_false(imv_crop_select(&g, -100, -100, 0, 0, &r));
  g_assert_false(imv_crop_select(&g, NAN, 0, 0, 0, &r));

  g_assert_true(imv_crop_geometry(image, -20, -10, 0.5, 0, false, &g));
  g_assert_true(imv_crop_select(&g, -19.5, -9.5, -18.5, -9, &r));
  g_assert_cmpint(r.x, ==, 1);
  g_assert_cmpint(r.y, ==, 1);
  g_assert_cmpint(r.width, ==, 2);
  g_assert_cmpint(r.height, ==, 1);
  GError *error = NULL;
  struct imv_bitmap result = imv_crop_extract(image, &g, &r, &error);
  g_assert_no_error(error);
  g_assert_cmpmem(result.data, 8, imv_image_get_bitmap(image)->data + 16, 8);
  imv_bitmap_free(result);
  g_assert_false(imv_crop_geometry(NULL, 0, 0, 1, 0, false, &g));
  g_assert_false(imv_crop_geometry(image, 0, 0, 0, 0, false, &g));
  imv_image_free(image);
}

static void test_transforms(void)
{
  const struct {
    double rotation;
    bool mirrored;
    int width, height;
    uint8_t pixels[6];
  } cases[] = {
    {0, false, 3, 2, {1, 2, 3, 4, 5, 6}},
    {0, true, 3, 2, {3, 2, 1, 6, 5, 4}},
    {90, false, 2, 3, {4, 1, 5, 2, 6, 3}},
    {90, true, 2, 3, {1, 4, 2, 5, 3, 6}},
    {180, false, 3, 2, {6, 5, 4, 3, 2, 1}},
    {-180, true, 3, 2, {4, 5, 6, 1, 2, 3}},
    {270, false, 2, 3, {3, 6, 2, 5, 1, 4}},
  };
  struct imv_image *image = sample();
  for (size_t i = 0; i < G_N_ELEMENTS(cases); ++i) {
    struct imv_crop_geometry g;
    g_assert_true(imv_crop_geometry(image, 30, 40, 2, cases[i].rotation,
        cases[i].mirrored, &g));
    g_assert_cmpint(g.width, ==, cases[i].width);
    g_assert_cmpint(g.height, ==, cases[i].height);
    struct imv_crop_rect r;
    g_assert_true(imv_crop_select(&g, g.x, g.y, g.x + g.width * g.scale,
        g.y + g.height * g.scale, &r));
    GError *error = NULL;
    struct imv_bitmap result = imv_crop_extract(image, &g, &r, &error);
    g_assert_no_error(error);
    for (int p = 0; p < 6; ++p) {
      const uint8_t *expected = imv_image_get_bitmap(image)->data + (cases[i].pixels[p] - 1) * 4;
      g_assert_cmpmem(result.data + p * 4, 4, expected, 4);
    }
    imv_bitmap_free(result);
  }
  imv_image_free(image);
}

static void test_save(void)
{
  char *directory = g_dir_make_tmp("imv-crop-test-XXXXXX", NULL);
  char *original = g_build_filename(directory, "photo 'quoted' $file;.jpeg", NULL);
  g_assert_true(g_file_set_contents(original, "original", -1, NULL));
  struct imv_image *image = sample();
  struct imv_crop_geometry g;
  g_assert_true(imv_crop_geometry(image, 0, 0, 1, 0, false, &g));
  struct imv_crop_rect r = {0, 0, 3, 2};
  GError *error = NULL;
  char *first = imv_crop_save(image, &g, &r, original, &error);
  g_assert_no_error(error);
  g_assert_true(g_str_has_suffix(first, "photo 'quoted' $file;-crop.png"));
  png_image png = {.version = PNG_IMAGE_VERSION};
  g_assert_true(png_image_begin_read_from_file(&png, first));
  g_assert_cmpuint(png.width, ==, 3);
  g_assert_cmpuint(png.height, ==, 2);
  png.format = PNG_FORMAT_RGBA;
  uint8_t pixels[24];
  g_assert_true(png_image_finish_read(&png, NULL, pixels, 0, NULL));
  g_assert_cmpmem(pixels, sizeof pixels, imv_image_get_bitmap(image)->data, sizeof pixels);
  png_image_free(&png);
  g_assert_true(g_file_set_contents(first, "existing crop", -1, NULL));
  char *second = imv_crop_save(image, &g, &r, original, &error);
  g_assert_no_error(error);
  g_assert_true(g_str_has_suffix(second, "-crop-2.png"));
  char *contents;
  g_assert_true(g_file_get_contents(original, &contents, NULL, NULL));
  g_assert_cmpstr(contents, ==, "original");
  g_free(contents);
  g_assert_true(g_file_get_contents(first, &contents, NULL, NULL));
  g_assert_cmpstr(contents, ==, "existing crop");
  g_free(contents);
  g_assert_cmpint(g_unlink(first), ==, 0);
  g_assert_cmpint(g_unlink(second), ==, 0);

  /* An existing symlink must not be followed or overwritten. */
  g_assert_cmpint(symlink(original, first), ==, 0);
  char *third = imv_crop_save(image, &g, &r, original, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(third, ==, second);
  g_assert_true(g_file_test(first, G_FILE_TEST_IS_SYMLINK));
  g_assert_cmpint(g_unlink(third), ==, 0);
  g_assert_cmpint(g_unlink(first), ==, 0);

  if (geteuid() != 0) {
    g_assert_cmpint(g_chmod(directory, 0500), ==, 0);
    g_assert_null(imv_crop_save(image, &g, &r, original, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error(&error);
    g_assert_cmpint(g_chmod(directory, 0700), ==, 0);
    g_assert_false(g_file_test(first, G_FILE_TEST_EXISTS));
  }
  r.width = 0;
  g_assert_null(imv_crop_save(image, &g, &r, original, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error(&error);
  g_assert_cmpint(g_unlink(original), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
  g_free(first);
  g_free(second);
  g_free(third);
  g_free(original);
  g_free(directory);
  imv_image_free(image);
}

#ifdef IMV_BACKEND_LIBRSVG
static void test_svg(void)
{
  const char *svg = "<svg xmlns='http://www.w3.org/2000/svg' width='4' height='2'>"
      "<rect x='2' width='2' height='2' fill='#ff0000'/></svg>";
  GError *error = NULL;
  RsvgHandle *handle = rsvg_handle_new_from_data((const uint8_t *)svg, strlen(svg), &error);
  g_assert_no_error(error);
  struct imv_image *image = imv_image_create_from_svg(handle);
  struct imv_crop_geometry g;
  g_assert_true(imv_crop_geometry(image, 0, 0, 1, 0, false, &g));
  struct imv_crop_rect r = {2, 0, 2, 2};
  struct imv_bitmap result = imv_crop_extract(image, &g, &r, &error);
  g_assert_no_error(error);
  for (int i = 0; i < 4; ++i) {
    const uint8_t red[] = {255, 0, 0, 255};
    g_assert_cmpmem(result.data + i * 4, 4, red, 4);
  }
  imv_bitmap_free(result);
  imv_image_free(image);
}
#endif

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/crop/selection-and-native-pixels", test_selection);
  g_test_add_func("/crop/rotation-and-mirroring", test_transforms);
  g_test_add_func("/crop/png-and-exclusive-save", test_save);
#ifdef IMV_BACKEND_LIBRSVG
  g_test_add_func("/crop/svg", test_svg);
#endif
  return g_test_run();
}
