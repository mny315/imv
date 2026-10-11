#include "log.h"

#include "list.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static struct list *g_log_clients = NULL;

struct log_client {
  imv_log_callback callback;
  void *data;
};

void imv_log(enum imv_log_level level, const char *fmt,  ...)
{
  assert(fmt);

  /* Exit early if no one's listening */
  if (!g_log_clients || !g_log_clients->len) {
    return;
  }

  va_list args;
  char buffer[16384];
  va_start(args, fmt);
  vsnprintf(buffer, sizeof buffer, fmt, args);
  va_end(args);

  for (size_t i = 0; i < g_log_clients->len; ++i) {
    struct log_client *client = g_log_clients->items[i];
    client->callback(level, buffer, client->data);
  }
}

void imv_log_add_log_callback(imv_log_callback callback, void *data)
{
  assert(callback);

  struct log_client *client = calloc(1, sizeof *client);
  client->callback = callback;
  client->data = data;

  if (!g_log_clients) {
    g_log_clients = list_create();
  }

  list_append(g_log_clients, client);
}

void imv_log_remove_log_callback(imv_log_callback callback)
{
  assert(callback);

  if (!callback || !g_log_clients) {
    return;
  }

  for (size_t i = 0; i < g_log_clients->len; ++i) {
    struct log_client *client = g_log_clients->items[i];
    if (client->callback == callback) {
      free(client);
      list_remove(g_log_clients, i);
      if (!g_log_clients->len) {
        list_free(g_log_clients);
        g_log_clients = NULL;
      }
      return;
    }
  }
}
