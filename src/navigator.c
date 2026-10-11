#include "navigator.h"

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <glib.h>

#include "list.h"
#include "platform.h"
#include "trash.h"

/* Some systems like GNU/Hurd don't define PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct nav_item {
  char *path;
};

struct trash_action {
  struct imv_trash_entry *entry;
  size_t index;
  char *previous;
  char *next;
};

struct imv_navigator {
  struct list *paths;
  struct list *trash_history;
  size_t cur_path;
  time_t last_change;
  time_t last_check;
  int last_move_direction;
  int changed;
  int wrapped;
};

struct imv_navigator *imv_navigator_create(void)
{
  struct imv_navigator *nav = calloc(1, sizeof *nav);
  nav->last_move_direction = 1;
  nav->paths = list_create();
  nav->trash_history = list_create();
  return nav;
}

static void free_trash_action(struct trash_action *action)
{
  imv_trash_free(action->entry);
  free(action->previous);
  free(action->next);
  free(action);
}

void imv_navigator_free(struct imv_navigator *nav)
{
  for (size_t i = 0; i < nav->paths->len; ++i) {
    struct nav_item *nav_item = nav->paths->items[i];
    free(nav_item->path);
  }
  list_deep_free(nav->paths);
  for (size_t i = 0; i < nav->trash_history->len; ++i) {
    free_trash_action(nav->trash_history->items[i]);
  }
  list_free(nav->trash_history);
  free(nav);
}

static char *absolute_path(const char *path)
{
  /* Resolve directory symlinks, but retain the final component. */
  char *directory = g_path_get_dirname(path);
  char *parent = realpath(directory, NULL);
  char *name = g_path_get_basename(path);
  char *absolute = parent ? g_build_filename(parent, name, NULL) :
      g_canonicalize_filename(path, NULL);
  free(parent);
  g_free(directory);
  g_free(name);
  return absolute;
}

static int add_item(struct imv_navigator *nav, const char *path)
{
  struct nav_item *nav_item = calloc(1, sizeof *nav_item);
  /* Keep the leaf symlink: Delete must trash the opened link, not its target. */
  struct stat st;
  nav_item->path = lstat(path, &st) == 0 ? absolute_path(path) : strdup(path);
  list_append(nav->paths, nav_item);
  if (nav->paths->len == 1) {
    nav->cur_path = 0;
    nav->changed = 1;
  }
  return 0;
}

struct ancestor {
  dev_t device;
  ino_t inode;
  const struct ancestor *parent;
};

static int add_paths(struct imv_navigator *nav, const char *path, int recursive,
    const struct ancestor *parent)
{
  struct stat path_info;
  if (stat(path, &path_info) != 0) {
    return parent ? (errno == ENOENT || errno == ELOOP || errno == ENOTDIR ? 0 : 1) :
        add_item(nav, path);
  }
  if (!S_ISDIR(path_info.st_mode)) {
    return S_ISREG(path_info.st_mode) ? add_item(nav, path) : 0;
  }
  if (parent && !recursive) {
    return 0;
  }
  for (const struct ancestor *a = parent; a; a = a->parent) {
    if (a->device == path_info.st_dev && a->inode == path_info.st_ino) {
      return 0;
    }
  }
  struct ancestor here = {path_info.st_dev, path_info.st_ino, parent};
  struct dirent **entries;
  int count = scandir(path, &entries, NULL, alphasort);
  if (count < 0) {
    return 1;
  }
  int result = 0;
  for (int i = 0; i < count; ++i) {
    const char *name = entries[i]->d_name;
    if (strcmp(name, ".") && strcmp(name, "..")) {
      char *child = g_build_filename(path, name, NULL);
      result |= add_paths(nav, child, recursive, &here);
      g_free(child);
    }
    free(entries[i]);
  }
  free(entries);
  return result;
}

int imv_navigator_add(struct imv_navigator *nav, const char *path, int recursive)
{
  return add_paths(nav, path, recursive, NULL);
}

const char *imv_navigator_selection(struct imv_navigator *nav)
{
  const char *path = imv_navigator_at(nav, nav->cur_path);
  return path ? path : "";
}

size_t imv_navigator_index(struct imv_navigator *nav)
{
  return nav->cur_path;
}

void imv_navigator_select_rel(struct imv_navigator *nav, ssize_t direction)
{
  const ssize_t prev_path = nav->cur_path;
  if (nav->paths->len == 0) {
    return;
  }

  if (direction > 1) {
    direction %= (ssize_t)nav->paths->len;
  } else if (direction < -1) {
    direction %= (ssize_t)nav->paths->len;
  } else if (direction == 0) {
    return;
  }

  ssize_t new_path = (ssize_t)nav->cur_path + direction;
  if (new_path >= (ssize_t)nav->paths->len) {
    /* Wrap after the end of the list */
    new_path -= (ssize_t)nav->paths->len;
    nav->wrapped = 1;
  } else if (new_path < 0) {
    /* Wrap before the start of the list */
    new_path += (ssize_t)nav->paths->len;
    nav->wrapped = 1;
  }
  nav->cur_path = (size_t)new_path;
  nav->last_move_direction = direction;
  nav->changed = nav->changed || prev_path != new_path;
  return;
}

void imv_navigator_select_abs(struct imv_navigator *nav, ssize_t index)
{
  if (!nav->paths->len) {
    nav->cur_path = 0;
    return;
  }
  /* allow -1 to indicate the last image */
  if (index < 0) {
    index += (ssize_t)nav->paths->len;

    /* but if they go farther back than the first image, stick to first image */
    if (index < 0) {
      index = 0;
    }
  }

  /* stick to last image if we go beyond it */
  if (index >= (ssize_t)nav->paths->len) {
    index = (ssize_t)nav->paths->len - 1;
  }

  const size_t prev_path = nav->cur_path;
  nav->cur_path = (size_t)index;
  nav->changed = prev_path != nav->cur_path || nav->changed;
  nav->last_move_direction = (index >= (ssize_t)prev_path) ? 1 : -1;
}

void imv_navigator_remove(struct imv_navigator *nav, const char *path)
{
  for (size_t i = 0; i < nav->paths->len; ++i) {
    struct nav_item *item = nav->paths->items[i];
    if (!strcmp(item->path, path)) {
      imv_navigator_remove_at(nav, i);
      return;
    }
  }
}

void imv_navigator_remove_at(struct imv_navigator *nav, size_t index)
{
  if (index >= nav->paths->len) {
    return;
  }
  struct nav_item *item = nav->paths->items[index];
  free(item->path);
  free(item);
  list_remove(nav->paths, index);

  if (!nav->paths->len) {
    nav->cur_path = 0;
  } else if (index < nav->cur_path) {
    --nav->cur_path;
  } else if (nav->cur_path == index) {
    /* We just removed the current path */
    if (nav->last_move_direction < 0) {
      /* Move left */
      imv_navigator_select_rel(nav, -1);
    } else {
      /* Try to stay where we are, unless we ran out of room */
      if (nav->cur_path == nav->paths->len) {
        nav->cur_path = 0;
        nav->wrapped = 1;
      }
    }
  }
  nav->changed = 1;
}

void imv_navigator_remove_all(struct imv_navigator *nav)
{
  for (size_t i = 0; i < nav->paths->len; ++i) {
    struct nav_item *item = nav->paths->items[i];
    free(item->path);
    free(item);
  }
  list_clear(nav->paths);
  nav->cur_path = 0;
  nav->changed = 1;
}

bool imv_navigator_trash(struct imv_navigator *nav)
{
  const char *path = imv_navigator_selection(nav);
  if (nav->changed || !*path || !strcmp(path, "-")) {
    return false;
  }

  struct imv_trash_entry *entry = imv_trash_file(path);
  if (!entry) {
    return false;
  }

  struct trash_action *action = calloc(1, sizeof *action);
  action->entry = entry;
  const char *trashed_path = imv_trash_original_path(entry);
  for (size_t i = 0; i < nav->paths->len; ++i) {
    const char *candidate = imv_navigator_at(nav, i);
    if (!strcmp(candidate, trashed_path)) {
      continue;
    }
    if (i < nav->cur_path) {
      free(action->previous);
      action->previous = strdup(candidate);
    } else if (i > nav->cur_path && !action->next) {
      action->next = strdup(candidate);
    }
  }
  size_t next = nav->cur_path;
  for (size_t i = nav->paths->len; i-- > 0;) {
    struct nav_item *item = nav->paths->items[i];
    if (!strcmp(item->path, trashed_path)) {
      if (i < nav->cur_path) {
        --next;
      }
      free(item->path);
      free(item);
      list_remove(nav->paths, i);
    }
  }
  action->index = next;
  list_append(nav->trash_history, action);

  nav->cur_path = next;
  if (next >= nav->paths->len) {
    nav->cur_path = 0;
  }
  /* Keep the window open for undo, including with -x. */
  nav->wrapped = 0;
  nav->last_move_direction = 1;
  nav->changed = 1;
  return true;
}

bool imv_navigator_can_undo_trash(struct imv_navigator *nav)
{
  return nav->trash_history->len > 0;
}

bool imv_navigator_undo_trash(struct imv_navigator *nav)
{
  if (!imv_navigator_can_undo_trash(nav)) {
    return false;
  }
  struct trash_action *action = nav->trash_history->items[nav->trash_history->len - 1];
  if (!imv_trash_restore(action->entry)) {
    return false;
  }

  const char *path = imv_trash_original_path(action->entry);
  ssize_t index = imv_navigator_find_path(nav, path);
  if (index == -1) {
    index = action->next ? imv_navigator_find_path(nav, action->next) : -1;
    if (index == -1 && action->previous) {
      index = imv_navigator_find_path(nav, action->previous);
      if (index >= 0) {
        ++index;
      }
    }
    if (index == -1) {
      index = action->index < nav->paths->len ? action->index : nav->paths->len;
    }
    struct nav_item *item = calloc(1, sizeof *item);
    item->path = strdup(path);
    list_insert(nav->paths, index, item);
  }
  imv_navigator_select_abs(nav, index);
  nav->changed = 1;
  nav->wrapped = 0;
  list_remove(nav->trash_history, nav->trash_history->len - 1);
  free_trash_action(action);
  return true;
}

ssize_t imv_navigator_find_path(struct imv_navigator *nav, const char *path)
{
  char *absolute = absolute_path(path);
  for (size_t i = 0; i < nav->paths->len; ++i) {
    const char *candidate = imv_navigator_at(nav, i);
    if (!strcmp(candidate, path) || !strcmp(candidate, absolute)) {
      g_free(absolute);
      return i;
    }
  }
  g_free(absolute);
  /* no exact matches or path cannot be resolved, try the final portion of the path */
  for (size_t i = 0; i < nav->paths->len; ++i) {
    struct nav_item *item = nav->paths->items[i];
    char *last_sep = strrchr(item->path, '/');
    if (last_sep && !strcmp(last_sep+1, path)) {
      return (ssize_t)i;
    }
  }

  /* no matches at all, give up */
  return -1;
}

int imv_navigator_poll_changed(struct imv_navigator *nav)
{
  if (nav->changed) {
    nav->changed = 0;
    nav->last_change = imv_time();
    return 1;
  }

  if (nav->paths->len == 0) {
    return 0;
  };

  const time_t cur_time = imv_time();
  /* limit polling to once per second */
  if (nav->last_check < cur_time) {
    nav->last_check = cur_time;

    struct nav_item *cur_item = nav->paths->items[nav->cur_path];
    const time_t file_changed = imv_file_last_modified(cur_item->path);

    if (file_changed > nav->last_change) {
      nav->last_change = file_changed;
      return 1;
    }
  }
  return 0;
}

bool imv_navigator_has_pending_change(const struct imv_navigator *nav)
{
  return nav->changed != 0;
}

int imv_navigator_wrapped(struct imv_navigator *nav)
{
  return nav->wrapped;
}

size_t imv_navigator_length(struct imv_navigator *nav)
{
  return nav->paths->len;
}

char *imv_navigator_at(struct imv_navigator *nav, size_t index)
{
  if (index < nav->paths->len) {
    struct nav_item *item = nav->paths->items[index];
    return item->path;
  }
  return NULL;
}

/* vim:set ts=2 sts=2 sw=2 et: */
