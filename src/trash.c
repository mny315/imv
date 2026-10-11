#include "trash.h"
#include "log.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct imv_trash_entry {
  char *original;
  char *file;
  char *info;
  struct stat identity;
};

static bool same_file(const struct stat *a, const struct stat *b)
{
  return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
      a->st_size == b->st_size &&
      a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
      a->st_mtim.tv_nsec == b->st_mtim.tv_nsec;
}

static bool original_matches(const char *info_path, const char *base,
    const char *original)
{
  GKeyFile *info = g_key_file_new();
  char *encoded = NULL;
  if (g_key_file_load_from_file(info, info_path, G_KEY_FILE_NONE, NULL)) {
    encoded = g_key_file_get_string(info, "Trash Info", "Path", NULL);
  }
  char *decoded = encoded ? g_uri_unescape_string(encoded, NULL) : NULL;
  char *path = decoded ? g_canonicalize_filename(decoded, base) : NULL;
  bool matches = g_strcmp0(path, original) == 0;
  g_free(path);
  g_free(decoded);
  g_free(encoded);
  g_key_file_unref(info);
  return matches;
}

static bool scan_directory(struct imv_trash_entry *entry, const char *root,
    const char *base, GHashTable *existing, bool snapshot)
{
  struct stat st;
  if (lstat(root, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
    return false;
  }
  char *info_dir = g_build_filename(root, "info", NULL);
  GDir *dir = g_dir_open(info_dir, 0, NULL);
  bool found = false;
  const char *name;
  while (dir && (name = g_dir_read_name(dir))) {
    if (!g_str_has_suffix(name, ".trashinfo")) {
      continue;
    }
    char *info_path = g_build_filename(info_dir, name, NULL);
    if (snapshot) {
      g_hash_table_add(existing, info_path);
      continue;
    }
    if (g_hash_table_contains(existing, info_path)) {
      g_free(info_path);
      continue;
    }
    char *basename = g_strndup(name, strlen(name) - strlen(".trashinfo"));
    char *file_path = g_build_filename(root, "files", basename, NULL);
    g_free(basename);
    if (!lstat(file_path, &st) && same_file(&st, &entry->identity) &&
        original_matches(info_path, base, entry->original)) {
      entry->file = file_path;
      entry->info = info_path;
      entry->identity = st;
      found = true;
      break;
    }
    g_free(file_path);
    g_free(info_path);
  }
  if (dir) {
    g_dir_close(dir);
  }
  g_free(info_dir);
  return found;
}

static void scan_trash(struct imv_trash_entry *entry, GHashTable *existing,
    bool snapshot)
{
  char *root = g_build_filename(g_get_user_data_dir(), "Trash", NULL);
  bool found = scan_directory(entry, root, g_get_user_data_dir(), existing, snapshot);
  g_free(root);
  if (found) {
    return;
  }

  /* Also cover trash on other volumes, including bind mounts. */
  char *base = g_path_get_dirname(entry->original);
  char *uid = g_strdup_printf("%lu", (unsigned long)geteuid());
  char *private_name = g_strdup_printf(".Trash-%s", uid);
  while (true) {
    char *shared = g_build_filename(base, ".Trash", NULL);
    struct stat st;
    if (!lstat(shared, &st) && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX)) {
      root = g_build_filename(shared, uid, NULL);
      found = scan_directory(entry, root, base, existing, snapshot);
      g_free(root);
    }
    g_free(shared);
    if (!found) {
      root = g_build_filename(base, private_name, NULL);
      found = scan_directory(entry, root, base, existing, snapshot);
      g_free(root);
    }
    if (found || !strcmp(base, "/")) {
      break;
    }
    char *parent = g_path_get_dirname(base);
    g_free(base);
    base = parent;
  }
  g_free(private_name);
  g_free(uid);
  g_free(base);
}

struct imv_trash_entry *imv_trash_file(const char *path)
{
  struct imv_trash_entry *entry = g_new0(struct imv_trash_entry, 1);
  entry->original = g_canonicalize_filename(path, NULL);
  if (lstat(path, &entry->identity)) {
    imv_log(IMV_ERROR, "Failed to trash '%s': %s\n", path, strerror(errno));
    imv_trash_free(entry);
    return NULL;
  }

  /* GIO does not return the new trash path. Snapshot existing entries so even
   * previous deletions of the same path/inode cannot be mistaken for this one. */
  GHashTable *existing = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  scan_trash(entry, existing, true);
  GFile *file = g_file_new_for_path(path);
  GError *error = NULL;
  bool success = g_file_trash(file, NULL, &error);
  g_object_unref(file);
  if (success) {
    scan_trash(entry, existing, false);
    if (!entry->file) {
      imv_log(IMV_WARNING, "File trashed, but its undo location was not found: %s\n", path);
    }
  } else {
    imv_log(IMV_ERROR, "Failed to move '%s' to the trash: %s\n", path, error->message);
    g_error_free(error);
    imv_trash_free(entry);
    entry = NULL;
  }
  g_hash_table_unref(existing);
  return entry;
}

bool imv_trash_restore(struct imv_trash_entry *entry)
{
  struct stat st;
  if (!entry->file || lstat(entry->file, &st) ||
      !same_file(&st, &entry->identity)) {
    imv_log(IMV_ERROR, "Cannot undo trash: the saved file is missing or changed: %s\n",
        entry->original);
    return false;
  }

  GFile *source = g_file_new_for_path(entry->file);
  GFile *destination = g_file_new_for_path(entry->original);
  GError *error = NULL;
  bool success = g_file_move(source, destination, G_FILE_COPY_NOFOLLOW_SYMLINKS,
      NULL, NULL, NULL, &error);
  g_object_unref(source);
  g_object_unref(destination);
  if (!success) {
    imv_log(IMV_ERROR, "Failed to restore '%s': %s\n", entry->original, error->message);
    g_error_free(error);
    return false;
  }
  if (g_unlink(entry->info) && errno != ENOENT) {
    imv_log(IMV_WARNING, "Restored '%s', but could not remove its trash metadata: %s\n",
        entry->original, strerror(errno));
  }
  return true;
}

const char *imv_trash_original_path(const struct imv_trash_entry *entry)
{
  return entry->original;
}

void imv_trash_free(struct imv_trash_entry *entry)
{
  if (entry) {
    g_free(entry->original);
    g_free(entry->file);
    g_free(entry->info);
    g_free(entry);
  }
}
