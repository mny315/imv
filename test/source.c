#include "source.h"
#include "source_private.h"

#include <glib.h>

struct state {
  GMutex mutex;
  GCond changed;
  bool loaded;
  bool allow_callback;
  bool callback_finished;
  bool freed;
  int source_id;
};

static void load_frame(void *private, struct imv_image **image, int *frametime)
{
  (void)image;
  (void)frametime;
  struct state *state = private;
  g_mutex_lock(&state->mutex);
  state->loaded = true;
  g_cond_signal(&state->changed);
  g_mutex_unlock(&state->mutex);
}

static void receive_frame(struct imv_source_message *message)
{
  struct state *state = message->user_data;
  g_mutex_lock(&state->mutex);
  while (!state->allow_callback) {
    g_cond_wait(&state->changed, &state->mutex);
  }
  /* Cancelling a source must not free it while the callback is still using it. */
  g_assert_false(state->freed);
  g_assert_cmpint(imv_source_get_id(message->source), ==, state->source_id);
  state->callback_finished = true;
  g_mutex_unlock(&state->mutex);
}

static void free_private(void *private)
{
  struct state *state = private;
  g_mutex_lock(&state->mutex);
  g_assert_true(state->callback_finished);
  state->freed = true;
  g_cond_signal(&state->changed);
  g_mutex_unlock(&state->mutex);
}

static void test_cancel_during_callback(const void *data)
{
  struct state state = {0};
  const struct imv_source_vtable vtable = {
    .load_first_frame = load_frame,
    .load_next_frame = load_frame,
    .free = free_private,
  };
  struct imv_source *source = imv_source_create(&vtable, &state);
  state.source_id = imv_source_get_id(source);
  imv_source_set_callback(source, receive_frame, &state);
  if (GPOINTER_TO_UINT(data)) {
    imv_source_async_load_next_frame(source);
  } else {
    imv_source_async_load_first_frame(source);
  }

  g_mutex_lock(&state.mutex);
  while (!state.loaded) {
    g_cond_wait(&state.changed, &state.mutex);
  }
  imv_source_async_free(source);

  /* Let cancellation run while the callback is held, then release it. */
  gint64 deadline = g_get_monotonic_time() + 20000;
  while (!state.freed && g_cond_wait_until(&state.changed, &state.mutex, deadline)) {
  }
  g_assert_false(state.freed);
  state.allow_callback = true;
  g_cond_signal(&state.changed);
  while (!state.freed) {
    g_cond_wait(&state.changed, &state.mutex);
  }
  g_mutex_unlock(&state.mutex);
  g_cond_clear(&state.changed);
  g_mutex_clear(&state.mutex);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_data_func("/source/cancel-first-frame-callback", GUINT_TO_POINTER(0),
      test_cancel_during_callback);
  g_test_add_data_func("/source/cancel-next-frame-callback", GUINT_TO_POINTER(1),
      test_cancel_during_callback);
  return g_test_run();
}
