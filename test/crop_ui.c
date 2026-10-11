/* Replay real command/input handlers with the dummy window, without a display. */
#include "../src/imv.c"

#include <glib/gstdio.h>
#include <png.h>
#include "source_private.h"

static void key(struct imv *imv, const char *name, const char *description)
{
  struct imv_event e = {.type = IMV_EVENT_KEYBOARD,
      .data.keyboard = {.keyname = (char *)name, .description = (char *)description,
          .text = ""}};
  event_handler(imv, &e);
}

static void button(struct imv *imv, bool pressed, double x, double y)
{
  struct imv_event e = {.type = IMV_EVENT_MOUSE_BUTTON,
      .data.mouse_button = {.button = 1, .pressed = pressed, .x = x, .y = y}};
  event_handler(imv, &e);
}

static void test_crop_input(void)
{
  char *directory = g_dir_make_tmp("imv-crop-ui-XXXXXX", NULL);
  char *original = g_build_filename(directory, "photo.jpeg", NULL);
  char *saved = g_build_filename(directory, "photo-crop.png", NULL);
  g_assert_true(g_file_set_contents(original, "original", -1, NULL));
  struct imv *imv = imv_create();
  imv->view = imv_viewport_create(20, 20);
  imv_navigator_add(imv->navigator, original, 0);
  imv_navigator_poll_changed(imv->navigator);
  struct imv_bitmap bitmap = imv_bitmap_alloc(6, 4);
  for (int i = 0; i < 24; ++i) {
    bitmap.data[i * 4] = i;
    bitmap.data[i * 4 + 1] = 100;
    bitmap.data[i * 4 + 2] = 200;
    bitmap.data[i * 4 + 3] = 255;
  }
  imv->current_image = imv_image_create_from_bitmap(bitmap);
  imv_viewport_scale_to_actual(imv->view, imv->current_image);
  imv->need_rescale = false;

  imv->loading = true;
  key(imv, "c", "Ctrl+c");
  g_assert_false(imv->crop.active);
  imv->loading = false;
  key(imv, "c", "Ctrl+c");
  g_assert_true(imv->crop.active);
  g_assert_false(imv_viewport_is_playing(imv->view));
  key(imv, "Return", "Return");
  g_assert_true(imv->crop.active);
  g_assert_false(g_file_test(saved, G_FILE_TEST_EXISTS));
  key(imv, "Delete", "Delete");
  key(imv, "Right", "Right");
  g_assert_cmpstr(imv_navigator_selection(imv->navigator), ==, original);
  g_assert_true(g_file_test(original, G_FILE_TEST_EXISTS));

  button(imv, true, 8, 9);
  struct imv_event move = {.type = IMV_EVENT_MOUSE_MOTION,
      .data.mouse_motion = {.x = 11, .y = 11, .dx = 3, .dy = 2}};
  event_handler(imv, &move);
  button(imv, false, 11, 11);
  int x, y;
  imv_viewport_get_offset(imv->view, &x, &y);
  g_assert_cmpint(x, ==, 7);
  g_assert_cmpint(y, ==, 8);
  g_assert_true(imv->crop.selected);
  g_assert_cmpint(imv->crop.rect.x, ==, 1);
  g_assert_cmpint(imv->crop.rect.y, ==, 1);
  g_assert_cmpint(imv->crop.rect.width, ==, 3);
  g_assert_cmpint(imv->crop.rect.height, ==, 2);
  key(imv, "Return", "Return");
  g_assert_false(imv->crop.active);
  g_assert_true(imv_viewport_is_playing(imv->view));
  g_assert_cmpuint(imv_navigator_length(imv->navigator), ==, 2);
  g_assert_cmpstr(imv_navigator_selection(imv->navigator), ==, original);

  png_image png = {.version = PNG_IMAGE_VERSION};
  g_assert_true(png_image_begin_read_from_file(&png, saved));
  g_assert_cmpuint(png.width, ==, 3);
  g_assert_cmpuint(png.height, ==, 2);
  png.format = PNG_FORMAT_RGBA;
  uint8_t pixels[24];
  g_assert_true(png_image_finish_read(&png, NULL, pixels, 0, NULL));
  g_assert_cmpmem(pixels, 12, bitmap.data + 7 * 4, 12);
  g_assert_cmpmem(pixels + 12, 12, bitmap.data + 13 * 4, 12);
  png_image_free(&png);

  /* Shipped configuration must retain the same shortcut. */
  g_assert_cmpint(ini_parse(IMV_TEST_CONFIG, handle_ini_value, imv), ==, 0);
  imv_viewport_set_playing(imv->view, false);
  key(imv, "c", "Ctrl+c");
  g_assert_true(imv->crop.active);
  button(imv, true, 11, 11);
  button(imv, false, 8, 9);
  g_assert_cmpint(imv->crop.rect.width, ==, 3);
  key(imv, "Escape", "Escape");
  g_assert_false(imv->crop.active);
  g_assert_false(imv_viewport_is_playing(imv->view));
  g_assert_cmpuint(imv_navigator_length(imv->navigator), ==, 2);

  /* A queued selection must not crop the previously displayed picture. */
  imv_navigator_select_rel(imv->navigator, 1);
  key(imv, "c", "Ctrl+c");
  g_assert_false(imv->crop.active);
  imv_navigator_select_abs(imv->navigator, 0);
  imv_navigator_poll_changed(imv->navigator);
  key(imv, "c", "Ctrl+c");
  button(imv, true, 8, 9);
  button(imv, false, 11, 11);
  if (geteuid() != 0) {
    g_assert_cmpint(g_chmod(directory, 0500), ==, 0);
    key(imv, "Return", "Return");
    g_assert_true(imv->crop.active);
    g_assert_true(imv->crop.selected);
    g_assert_nonnull(imv->notice);
    g_assert_cmpint(g_chmod(directory, 0700), ==, 0);
  }
  key(imv, "c", "Ctrl+c");
  g_assert_false(imv->crop.active);

  key(imv, "c", "Ctrl+c");
  g_assert_true(imv->crop.active);
  g_assert_cmpint(imv_command_exec(imv->commands, "rotate by 90", imv), ==, 0);
  g_assert_false(imv->crop.active);

  char *contents;
  g_assert_true(g_file_get_contents(original, &contents, NULL, NULL));
  g_assert_cmpstr(contents, ==, "original");
  g_free(contents);
  imv_log_remove_log_callback(log_to_stderr);
  imv_free(imv);
  g_assert_cmpint(g_unlink(saved), ==, 0);
  g_assert_cmpint(g_unlink(original), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
  g_free(saved);
  g_free(original);
  g_free(directory);
}

static void test_rebind_self(void)
{
  struct imv *imv = imv_create();
  bool overlay = imv->overlay.enabled;
  g_assert_true(add_bind(imv, "z", "bind z quit; overlay"));
  key(imv, "z", "z");
  g_assert_false(imv->quit);
  g_assert_cmpint(imv->overlay.enabled, ==, !overlay);
  key(imv, "z", "z");
  g_assert_true(imv->quit);
  imv_free(imv);
}

static void free_nothing(void *data) { (void)data; }

static void test_stale_reply(void)
{
  struct imv *imv = imv_create();
  struct imv_source_vtable vtable = {.free = free_nothing};
  imv->current_source = imv_source_create(&vtable, NULL);
  imv_navigator_add(imv->navigator, "missing-old", 0);
  imv_navigator_add(imv->navigator, "missing-new", 0);
  imv_navigator_poll_changed(imv->navigator);
  imv_navigator_select_rel(imv->navigator, 1);
  struct internal_event *event = calloc(1, sizeof *event);
  event->type = BAD_IMAGE;
  event->source_id = imv_source_get_id(imv->current_source);
  consume_internal_event(imv, event);
  g_assert_cmpuint(imv_navigator_length(imv->navigator), ==, 2);
  g_assert_cmpstr(imv_navigator_selection(imv->navigator), ==, "missing-new");
  imv_free(imv);
}

struct load_state {
  GMutex mutex;
  GCond changed;
  bool entered, release, freed;
};

static void delayed_load(void *data, struct imv_image **image, int *frametime)
{
  struct load_state *state = data;
  g_mutex_lock(&state->mutex);
  state->entered = true;
  g_cond_signal(&state->changed);
  while (!state->release) g_cond_wait(&state->changed, &state->mutex);
  g_mutex_unlock(&state->mutex);
  *image = imv_image_create_from_bitmap(imv_bitmap_alloc(1, 1));
  *frametime = 0;
}

static void delayed_free(void *data)
{
  ((struct load_state *)data)->freed = true;
}

static gpointer close_viewer(gpointer data)
{
  imv_free(data);
  return NULL;
}

static void test_close_during_load(void)
{
  struct load_state states[2] = {0};
  struct imv_source_vtable vtable = {.load_first_frame = delayed_load, .free = delayed_free};
  struct imv *imv = imv_create();
  for (int i = 0; i < 2; ++i) {
    struct imv_source *source = imv_source_create(&vtable, &states[i]);
    imv_source_set_callback(source, source_callback, imv);
    imv->current_source = source;
    imv_source_async_load_first_frame(source);
    g_mutex_lock(&states[i].mutex);
    while (!states[i].entered) g_cond_wait(&states[i].changed, &states[i].mutex);
    g_mutex_unlock(&states[i].mutex);
    if (i == 0) discard_current_source(imv);
  }
  GThread *closer = g_thread_new("close-viewer", close_viewer, imv);
  while (!g_atomic_int_get(&imv->shutting_down)) g_thread_yield();
  for (int i = 0; i < 2; ++i) {
    g_mutex_lock(&states[i].mutex);
    states[i].release = true;
    g_cond_signal(&states[i].changed);
    g_mutex_unlock(&states[i].mutex);
  }
  g_thread_join(closer);
  for (int i = 0; i < 2; ++i) {
    g_assert_true(states[i].freed);
    g_cond_clear(&states[i].changed);
    g_mutex_clear(&states[i].mutex);
  }
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/crop/input-and-save", test_crop_input);
  g_test_add_func("/lifecycle/stale-reply", test_stale_reply);
  g_test_add_func("/lifecycle/close-during-load", test_close_during_load);
  g_test_add_func("/commands/rebind-self", test_rebind_self);
  return g_test_run();
}
