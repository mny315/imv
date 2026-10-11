#include "../src/wl_window.c"
#include <xkbcommon/xkbcommon.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

static struct imv_event pop(struct imv_window *window)
{
  struct imv_event event;
  assert(imv_event_queue_pop(window->events, &event));
  return event;
}

static void test_pointer(void)
{
  struct imv_window window = {.scale = 2};
  window.events = imv_event_queue_create();
  assert(window.events);
  window.pointer.current.x = wl_fixed_from_int(10);
  window.pointer.current.y = wl_fixed_from_int(20);
  pointer_button(&window, NULL, 0, 0, 0x110, 1);
  pointer_motion(&window, NULL, 0, wl_fixed_from_int(30), wl_fixed_from_int(50));
  pointer_button(&window, NULL, 0, 0, 0x110, 0);
  pointer_frame(&window, NULL);
  struct imv_event e = pop(&window);
  assert(e.type == IMV_EVENT_MOUSE_BUTTON && e.data.mouse_button.pressed);
  assert(e.data.mouse_button.x == 20 && e.data.mouse_button.y == 40);
  e = pop(&window);
  assert(e.type == IMV_EVENT_MOUSE_BUTTON && !e.data.mouse_button.pressed);
  assert(e.data.mouse_button.x == 60 && e.data.mouse_button.y == 100);
  e = pop(&window);
  assert(e.type == IMV_EVENT_MOUSE_MOTION);
  assert(e.data.mouse_motion.x == 60 && e.data.mouse_motion.y == 100);
  pointer_frame(&window, NULL);
  assert(!imv_event_queue_pop(window.events, &e));
  pointer_button(&window, NULL, 0, 0, 0x110, 1);
  e = pop(&window);
  assert(e.type == IMV_EVENT_MOUSE_BUTTON && e.data.mouse_button.pressed);
  for (int i = 1; i <= 3; ++i) {
    pointer_motion(&window, NULL, 0, wl_fixed_from_int(30 + i), wl_fixed_from_int(50 + i));
    pointer_frame(&window, NULL);
    e = pop(&window);
    assert(e.type == IMV_EVENT_MOUSE_MOTION);
    assert(!imv_event_queue_pop(window.events, &e));
  }
  pointer_button(&window, NULL, 0, 0, 0x110, 0);
  e = pop(&window);
  assert(e.type == IMV_EVENT_MOUSE_BUTTON && !e.data.mouse_button.pressed);
  imv_event_queue_free(window.events);
}

static void test_repeat(void)
{
  struct imv_window window = {0};
  window.events = imv_event_queue_create();
  window.keyboard = imv_keyboard_create();
  struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  struct xkb_rule_names names = {.layout = "us"};
  struct xkb_keymap *map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  g_assert_nonnull(map);
  char *keymap = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
  imv_keyboard_set_keymap(window.keyboard, keymap);
  free(keymap);
  xkb_keymap_unref(map);
  xkb_context_unref(ctx);
  imv_keyboard_set_keymap(window.keyboard, "invalid keymap");
  keyboard_repeat(&window, NULL, 25, 1200);
  gint64 now = g_get_monotonic_time();
  keyboard_key(&window, NULL, 0, 0, 30, 1);
  struct imv_event e = pop(&window);
  g_assert_cmpstr(e.data.keyboard.keyname, ==, "a");
  cleanup_event(&e);
  g_assert_cmpint(window.next_repeat, >=, now + 1200000);
  /* Releasing another key must not cancel the held key's repeat. */
  keyboard_key(&window, NULL, 0, 0, 42, 0);
  g_assert_cmpint(window.next_repeat, >, 0);
  window.next_repeat = 1;
  push_repeat(&window);
  e = pop(&window);
  g_assert_cmpstr(e.data.keyboard.keyname, ==, "a");
  cleanup_event(&e);
  keyboard_leave(&window, NULL, 0, NULL);
  g_assert_cmpint(window.next_repeat, ==, 0);
  keyboard_repeat(&window, NULL, 0, 500);
  keyboard_key(&window, NULL, 0, 0, 30, 1);
  e = pop(&window);
  cleanup_event(&e);
  g_assert_cmpint(window.next_repeat, ==, 0);
  push_repeat(&window);
  g_assert_false(imv_event_queue_pop(window.events, &e));
  imv_keyboard_free(window.keyboard);
  imv_event_queue_free(window.events);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/wayland/pointer", test_pointer);
  g_test_add_func("/wayland/key-repeat", test_repeat);
  return g_test_run();
}
