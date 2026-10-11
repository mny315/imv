#include "navigator.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

static char *test_root;
static char *data_home;

struct fixture {
  struct imv_navigator *nav;
  char *directory;
  char *paths[4];
};

static void setup(struct fixture *fixture, const void *data)
{
  (void)data;
  fixture->directory = g_build_filename(test_root, "images with spaces-XXXXXX", NULL);
  g_assert_nonnull(g_mkdtemp(fixture->directory));
  fixture->nav = imv_navigator_create();
  const char *names[] = {"a.png", "b 'quoted' $file;.png", "c.png", "d.png"};
  for (size_t i = 0; i < G_N_ELEMENTS(names); ++i) {
    fixture->paths[i] = g_build_filename(fixture->directory, names[i], NULL);
    g_assert_true(g_file_set_contents(fixture->paths[i], names[i], -1, NULL));
    g_assert_cmpint(imv_navigator_add(fixture->nav, fixture->paths[i], 0), ==, 0);
  }
  g_assert_true(imv_navigator_poll_changed(fixture->nav));
}

static void teardown(struct fixture *fixture, const void *data)
{
  (void)data;
  imv_navigator_free(fixture->nav);
  for (size_t i = 0; i < G_N_ELEMENTS(fixture->paths); ++i) {
    g_unlink(fixture->paths[i]);
    g_free(fixture->paths[i]);
  }
  g_assert_cmpint(g_rmdir(fixture->directory), ==, 0);
  g_free(fixture->directory);
}

static void select_image(struct fixture *fixture, size_t index)
{
  imv_navigator_select_abs(fixture->nav, index);
  imv_navigator_poll_changed(fixture->nav);
}

static void assert_recoverable(const char *path)
{
  char *info_dir = g_build_filename(data_home, "Trash", "info", NULL);
  GDir *dir = g_dir_open(info_dir, 0, NULL);
  g_assert_nonnull(dir);
  bool found = false;
  const char *name;
  while ((name = g_dir_read_name(dir))) {
    char *info_path = g_build_filename(info_dir, name, NULL);
    GKeyFile *info = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(info, info_path, G_KEY_FILE_NONE, NULL));
    char *encoded = g_key_file_get_string(info, "Trash Info", "Path", NULL);
    char *original = g_uri_unescape_string(encoded, NULL);
    if (g_strcmp0(original, path) == 0) {
      char *date = g_key_file_get_string(info, "Trash Info", "DeletionDate", NULL);
      g_assert_nonnull(date);
      g_free(date);
      g_assert_true(g_str_has_suffix(name, ".trashinfo"));
      char *basename = g_strndup(name, strlen(name) - strlen(".trashinfo"));
      char *trashed = g_build_filename(data_home, "Trash", "files", basename, NULL);
      char *contents = NULL;
      g_assert_true(g_file_get_contents(trashed, &contents, NULL, NULL));
      char *expected = g_path_get_basename(path);
      g_assert_cmpstr(contents, ==, expected);
      g_free(expected);
      g_free(contents);
      g_free(trashed);
      g_free(basename);
      found = true;
    }
    g_free(original);
    g_free(encoded);
    g_key_file_unref(info);
    g_free(info_path);
  }
  g_dir_close(dir);
  g_free(info_dir);
  g_assert_true(found);
}

static void test_middle(struct fixture *fixture, const void *data)
{
  (void)data;
  select_image(fixture, 2);
  imv_navigator_select_rel(fixture->nav, -1);
  imv_navigator_poll_changed(fixture->nav);
  g_assert_true(imv_navigator_trash(fixture->nav));
  g_assert_false(g_file_test(fixture->paths[1], G_FILE_TEST_EXISTS));
  assert_recoverable(fixture->paths[1]);
  g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 3);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[2]);

  /* A second Delete before the next image is loaded must do nothing. */
  g_assert_false(imv_navigator_trash(fixture->nav));
  g_assert_true(g_file_test(fixture->paths[2], G_FILE_TEST_EXISTS));
  g_assert_true(imv_navigator_poll_changed(fixture->nav));
  imv_navigator_select_rel(fixture->nav, -1);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[0]);
}

static void test_edge(struct fixture *fixture, const void *data)
{
  size_t index = GPOINTER_TO_UINT(data);
  select_image(fixture, index);
  g_assert_true(imv_navigator_trash(fixture->nav));
  size_t next = index == 0 ? 1 : 0;
  size_t previous = index == 0 ? 3 : 2;
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[next]);
  imv_navigator_select_rel(fixture->nav, -1);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[previous]);
}

static void test_duplicates(struct fixture *fixture, const void *data)
{
  (void)data;
  imv_navigator_remove_all(fixture->nav);
  const size_t order[] = {1, 0, 1, 2, 1, 3};
  for (size_t i = 0; i < G_N_ELEMENTS(order); ++i) {
    imv_navigator_add(fixture->nav, fixture->paths[order[i]], 0);
  }
  select_image(fixture, 2);
  g_assert_true(imv_navigator_trash(fixture->nav));
  assert_recoverable(fixture->paths[1]);
  g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 3);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[2]);
  imv_navigator_select_rel(fixture->nav, -1);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[0]);
}

static void test_last_remaining(struct fixture *fixture, const void *data)
{
  (void)data;
  imv_navigator_remove_all(fixture->nav);
  imv_navigator_add(fixture->nav, fixture->paths[0], 0);
  imv_navigator_add(fixture->nav, fixture->paths[0], 0);
  select_image(fixture, 1);
  g_assert_true(imv_navigator_trash(fixture->nav));
  g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 0);
  g_assert_cmpuint(imv_navigator_index(fixture->nav), ==, 0);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, "");
  g_assert_false(imv_navigator_trash(fixture->nav));
}

static void test_failure(struct fixture *fixture, const void *data)
{
  (void)data;
  select_image(fixture, 1);
  g_assert_cmpint(g_unlink(fixture->paths[1]), ==, 0);
  g_assert_false(imv_navigator_trash(fixture->nav));
  g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 4);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[1]);
  g_assert_false(imv_navigator_poll_changed(fixture->nav));
}

static void test_pending_and_stdin(struct fixture *fixture, const void *data)
{
  (void)data;
  imv_navigator_select_rel(fixture->nav, 1);
  g_assert_false(imv_navigator_trash(fixture->nav));
  g_assert_true(g_file_test(fixture->paths[1], G_FILE_TEST_EXISTS));
  imv_navigator_remove_all(fixture->nav);
  imv_navigator_poll_changed(fixture->nav);
  g_assert_false(imv_navigator_trash(fixture->nav));
  imv_navigator_add(fixture->nav, "-", 0);
  imv_navigator_poll_changed(fixture->nav);
  g_assert_false(imv_navigator_trash(fixture->nav));
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, "-");
}

static void test_permission_failure(struct fixture *fixture, const void *data)
{
  (void)data;
  if (geteuid() == 0) {
    g_test_skip("Directory permissions do not restrict root");
    return;
  }
  select_image(fixture, 1);
  g_assert_cmpint(g_chmod(fixture->directory, 0500), ==, 0);
  g_assert_false(imv_navigator_trash(fixture->nav));
  g_assert_true(g_file_test(fixture->paths[1], G_FILE_TEST_EXISTS));
  g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 4);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[1]);
  g_assert_false(imv_navigator_poll_changed(fixture->nav));
  g_assert_cmpint(g_chmod(fixture->directory, 0700), ==, 0);
}

static void assert_contents(const char *path, const char *expected)
{
  char *contents = NULL;
  g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
  g_assert_cmpstr(contents, ==, expected);
  g_free(contents);
}

/* Return a trash location for this fixture, never the user's real trash. */
static char *find_trashed_file(const char *path)
{
  char *info_dir = g_build_filename(data_home, "Trash", "info", NULL);
  GDir *dir = g_dir_open(info_dir, 0, NULL);
  char *result = NULL;
  const char *name;
  while (dir && (name = g_dir_read_name(dir))) {
    char *info_path = g_build_filename(info_dir, name, NULL);
    GKeyFile *info = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(info, info_path, G_KEY_FILE_NONE, NULL));
    char *encoded = g_key_file_get_string(info, "Trash Info", "Path", NULL);
    char *original = g_uri_unescape_string(encoded, NULL);
    if (g_strcmp0(original, path) == 0) {
      char *basename = g_strndup(name, strlen(name) - strlen(".trashinfo"));
      result = g_build_filename(data_home, "Trash", "files", basename, NULL);
      g_free(basename);
    }
    g_free(original);
    g_free(encoded);
    g_key_file_unref(info);
    g_free(info_path);
    if (result) {
      break;
    }
  }
  if (dir) {
    g_dir_close(dir);
  }
  g_free(info_dir);
  return result;
}

static void test_undo(struct fixture *fixture, const void *data)
{
  size_t index = GPOINTER_TO_UINT(data);
  g_assert_false(imv_navigator_can_undo_trash(fixture->nav));
  g_assert_false(imv_navigator_undo_trash(fixture->nav));
  select_image(fixture, index);
  g_assert_true(imv_navigator_trash(fixture->nav));
  g_assert_true(imv_navigator_can_undo_trash(fixture->nav));

  /* Undo immediately, before the next image has even started loading. */
  g_assert_true(imv_navigator_undo_trash(fixture->nav));
  char *basename = g_path_get_basename(fixture->paths[index]);
  assert_contents(fixture->paths[index], basename);
  g_free(basename);
  g_assert_null(find_trashed_file(fixture->paths[index]));
  g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 4);
  for (size_t i = 0; i < 4; ++i) {
    g_assert_cmpstr(imv_navigator_at(fixture->nav, i), ==, fixture->paths[i]);
  }
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[index]);
  g_assert_false(imv_navigator_wrapped(fixture->nav));
  g_assert_true(imv_navigator_poll_changed(fixture->nav));
  g_assert_false(imv_navigator_can_undo_trash(fixture->nav));
  g_assert_false(imv_navigator_undo_trash(fixture->nav));
  imv_navigator_select_rel(fixture->nav, -1);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==,
      fixture->paths[(index + 3) % 4]);
  imv_navigator_select_rel(fixture->nav, 1);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[index]);
}

static void test_undo_all(struct fixture *fixture, const void *data)
{
  (void)data;
  for (size_t i = 0; i < 4; ++i) {
    select_image(fixture, 0);
    g_assert_true(imv_navigator_trash(fixture->nav));
  }
  g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 0);
  for (size_t i = 4; i-- > 0;) {
    g_assert_true(imv_navigator_undo_trash(fixture->nav));
    g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[i]);
    g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 4 - i);
  }
  for (size_t i = 0; i < 4; ++i) {
    g_assert_cmpstr(imv_navigator_at(fixture->nav, i), ==, fixture->paths[i]);
  }
  g_assert_false(imv_navigator_can_undo_trash(fixture->nav));
}

static void test_undo_conflict(struct fixture *fixture, const void *data)
{
  (void)data;
  select_image(fixture, 1);
  g_assert_true(imv_navigator_trash(fixture->nav));
  imv_navigator_poll_changed(fixture->nav);
  g_assert_true(g_file_set_contents(fixture->paths[1], "new occupant", -1, NULL));
  g_assert_false(imv_navigator_undo_trash(fixture->nav));
  assert_contents(fixture->paths[1], "new occupant");
  assert_recoverable(fixture->paths[1]);
  g_assert_true(imv_navigator_can_undo_trash(fixture->nav));
  g_assert_cmpuint(imv_navigator_length(fixture->nav), ==, 3);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[2]);
  g_assert_false(imv_navigator_poll_changed(fixture->nav));

  g_assert_cmpint(g_unlink(fixture->paths[1]), ==, 0);
  g_assert_true(imv_navigator_undo_trash(fixture->nav));
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[1]);
}

static void test_undo_old_copy(struct fixture *fixture, const void *data)
{
  (void)data;
  /* An older deletion by another app must remain in the trash. */
  GFile *file = g_file_new_for_path(fixture->paths[1]);
  g_assert_true(g_file_trash(file, NULL, NULL));
  g_object_unref(file);
  g_assert_true(g_file_set_contents(fixture->paths[1], "new version", -1, NULL));
  select_image(fixture, 1);
  g_assert_true(imv_navigator_trash(fixture->nav));
  g_assert_true(imv_navigator_undo_trash(fixture->nav));
  assert_contents(fixture->paths[1], "new version");
  assert_recoverable(fixture->paths[1]);
}

static void test_undo_missing_or_changed(struct fixture *fixture, const void *data)
{
  select_image(fixture, 1);
  g_assert_true(imv_navigator_trash(fixture->nav));
  imv_navigator_poll_changed(fixture->nav);
  char *trashed = find_trashed_file(fixture->paths[1]);
  g_assert_nonnull(trashed);
  if (GPOINTER_TO_UINT(data)) {
    g_assert_true(g_file_set_contents(trashed, "changed in trash", -1, NULL));
  } else {
    g_assert_cmpint(g_unlink(trashed), ==, 0);
  }
  g_assert_false(imv_navigator_undo_trash(fixture->nav));
  g_assert_true(imv_navigator_can_undo_trash(fixture->nav));
  g_assert_false(g_file_test(fixture->paths[1], G_FILE_TEST_EXISTS));
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, fixture->paths[2]);
  g_assert_false(imv_navigator_poll_changed(fixture->nav));
  g_free(trashed);
}

static void test_undo_after_close(struct fixture *fixture, const void *data)
{
  (void)data;
  select_image(fixture, 1);
  g_assert_true(imv_navigator_trash(fixture->nav));
  imv_navigator_remove(fixture->nav, fixture->paths[0]);
  g_assert_true(imv_navigator_undo_trash(fixture->nav));
  g_assert_cmpuint(imv_navigator_index(fixture->nav), ==, 0);
  g_assert_cmpstr(imv_navigator_at(fixture->nav, 1), ==, fixture->paths[2]);
}

static void test_undo_hardlinks(struct fixture *fixture, const void *data)
{
  (void)data;
  g_assert_cmpint(g_unlink(fixture->paths[1]), ==, 0);
  g_assert_cmpint(link(fixture->paths[0], fixture->paths[1]), ==, 0);
  select_image(fixture, 0);
  g_assert_true(imv_navigator_trash(fixture->nav));
  imv_navigator_poll_changed(fixture->nav);
  g_assert_true(imv_navigator_trash(fixture->nav));
  g_assert_true(imv_navigator_undo_trash(fixture->nav));
  g_assert_true(imv_navigator_undo_trash(fixture->nav));
  assert_contents(fixture->paths[0], "a.png");
  assert_contents(fixture->paths[1], "a.png");
}

static void test_undo_full_history(struct fixture *fixture, const void *data)
{
  (void)data;
  imv_navigator_remove_all(fixture->nav);
  GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
  for (size_t i = 0; i < 64; ++i) {
    char *path = g_strdup_printf("%s/frame-%zu.png", fixture->directory, i);
    g_ptr_array_add(paths, path);
    g_assert_true(g_file_set_contents(path, "image", -1, NULL));
    imv_navigator_add(fixture->nav, path, 0);
  }
  for (size_t i = 0; i < paths->len; ++i) {
    imv_navigator_poll_changed(fixture->nav);
    g_assert_true(imv_navigator_trash(fixture->nav));
  }
  for (size_t i = paths->len; i-- > 0;) {
    g_assert_true(imv_navigator_undo_trash(fixture->nav));
    g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, paths->pdata[i]);
    g_assert_cmpint(g_unlink(paths->pdata[i]), ==, 0);
  }
  g_assert_false(imv_navigator_can_undo_trash(fixture->nav));
  g_ptr_array_free(paths, true);
}

static void remove_test_directory(const char *path)
{
  GDir *dir = g_dir_open(path, 0, NULL);
  if (!dir) {
    g_assert_cmpint(g_unlink(path), ==, 0);
    return;
  }
  const char *name;
  while ((name = g_dir_read_name(dir))) {
    char *child = g_build_filename(path, name, NULL);
    remove_test_directory(child);
    g_free(child);
  }
  g_dir_close(dir);
  g_assert_cmpint(g_rmdir(path), ==, 0);
}

static void test_symlink(struct fixture *fixture, const void *data)
{
  (void)data;
  char *link = g_build_filename(fixture->directory, "link.png", NULL);
  g_assert_cmpint(symlink(fixture->paths[0], link), ==, 0);
  imv_navigator_add(fixture->nav, link, 0);
  select_image(fixture, 4);
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, link);
  g_assert_true(imv_navigator_trash(fixture->nav));
  g_assert_false(g_file_test(link, G_FILE_TEST_IS_SYMLINK));
  g_assert_true(g_file_test(fixture->paths[0], G_FILE_TEST_EXISTS));
  g_assert_true(imv_navigator_undo_trash(fixture->nav));
  g_assert_true(g_file_test(link, G_FILE_TEST_IS_SYMLINK));
  g_assert_cmpstr(imv_navigator_selection(fixture->nav), ==, link);
  g_unlink(link);
  g_free(link);
}

int main(int argc, char **argv)
{
  test_root = g_dir_make_tmp("imv-trash-test-XXXXXX", NULL);
  g_assert_nonnull(test_root);
  data_home = g_build_filename(test_root, "data", NULL);
  g_assert_cmpint(g_mkdir(data_home, 0700), ==, 0);
  g_setenv("XDG_DATA_HOME", data_home, true);
  g_setenv("GIO_USE_VFS", "local", true);
  g_test_init(&argc, &argv, NULL);

  g_test_add("/trash/middle-after-backwards-navigation", struct fixture, NULL,
      setup, test_middle, teardown);
  g_test_add("/trash/first", struct fixture, GUINT_TO_POINTER(0),
      setup, test_edge, teardown);
  g_test_add("/trash/last", struct fixture, GUINT_TO_POINTER(3),
      setup, test_edge, teardown);
  g_test_add("/trash/duplicates", struct fixture, NULL,
      setup, test_duplicates, teardown);
  g_test_add("/trash/last-remaining", struct fixture, NULL,
      setup, test_last_remaining, teardown);
  g_test_add("/trash/failure", struct fixture, NULL,
      setup, test_failure, teardown);
  g_test_add("/trash/pending-and-stdin", struct fixture, NULL,
      setup, test_pending_and_stdin, teardown);
  g_test_add("/trash/permission-failure", struct fixture, NULL,
      setup, test_permission_failure, teardown);
  g_test_add("/trash/undo/first", struct fixture, GUINT_TO_POINTER(0),
      setup, test_undo, teardown);
  g_test_add("/trash/undo/middle", struct fixture, GUINT_TO_POINTER(1),
      setup, test_undo, teardown);
  g_test_add("/trash/undo/last", struct fixture, GUINT_TO_POINTER(3),
      setup, test_undo, teardown);
  g_test_add("/trash/undo/all", struct fixture, NULL,
      setup, test_undo_all, teardown);
  g_test_add("/trash/undo/conflict-and-retry", struct fixture, NULL,
      setup, test_undo_conflict, teardown);
  g_test_add("/trash/undo/old-copy", struct fixture, NULL,
      setup, test_undo_old_copy, teardown);
  g_test_add("/trash/undo/missing", struct fixture, GUINT_TO_POINTER(0),
      setup, test_undo_missing_or_changed, teardown);
  g_test_add("/trash/undo/changed", struct fixture, GUINT_TO_POINTER(1),
      setup, test_undo_missing_or_changed, teardown);
  g_test_add("/trash/undo/after-close", struct fixture, NULL,
      setup, test_undo_after_close, teardown);
  g_test_add("/trash/undo/hardlinks", struct fixture, NULL,
      setup, test_undo_hardlinks, teardown);
  g_test_add("/trash/undo/full-history", struct fixture, NULL,
      setup, test_undo_full_history, teardown);
  g_test_add("/trash/undo/symlink", struct fixture, NULL,
      setup, test_symlink, teardown);
  int result = g_test_run();

  remove_test_directory(test_root);
  g_free(data_home);
  g_free(test_root);
  return result;
}
