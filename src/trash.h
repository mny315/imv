#ifndef IMV_TRASH_H
#define IMV_TRASH_H

#include <stdbool.h>

struct imv_trash_entry;

/* Trash a local file and retain its exact location for undo. NULL on failure. */
struct imv_trash_entry *imv_trash_file(const char *path);

/* Restore the retained file without overwriting an existing destination. */
bool imv_trash_restore(struct imv_trash_entry *entry);

const char *imv_trash_original_path(const struct imv_trash_entry *entry);

/* Release the record; never remove the file from the trash. */
void imv_trash_free(struct imv_trash_entry *entry);

#endif
