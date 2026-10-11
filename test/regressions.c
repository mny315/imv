#include "bitmap.h"
#include "binds.h"
#include "commands.h"
#include "console.h"
#include "event_queue.h"
#include "list.h"
#include "navigator.h"
#include "viewport.h"
#include "window.h"

#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <sys/stat.h>

static void test_print_bindings(void)
{
  struct list *keys = imv_bind_parse_keys("g<Ctrl+x>z");
  char full[32];
  g_assert_cmpuint(imv_bind_print_keylist(keys, full, sizeof full), ==, 10);
  g_assert_cmpstr(full, ==, "g<Ctrl+x>z");
  char small[4];
  g_assert_cmpuint(imv_bind_print_keylist(keys, small, sizeof small), ==, 10);
  g_assert_cmpstr(small, ==, "g<C");
  g_assert_cmpuint(imv_bind_print_keylist(keys, NULL, 0), ==, 10);
  list_deep_free(keys);
}

static void test_long_join(void)
{
  struct list *list = list_create();
  char *part = g_strnfill(16384, 'x');
  list_append(list, part);
  list_append(list, part);
  char *joined = list_to_string(list, "::", 0);
  g_assert_cmpuint(strlen(joined), ==, 32772);
  g_assert_cmpmem(joined, 16384, part, 16384);
  g_assert_cmpmem(joined + 16384, 2, "::", 2);
  g_assert_cmpmem(joined + 16386, 16384, part, 16384);
  free(joined);
  g_free(part);
  list_free(list);
}

static void capture_command(struct list *args, const char *argstr, void *data)
{
  g_assert_cmpstr(args->items[0], ==, "open");
  g_assert_cmpstr(argstr, ==, data);
}

static void test_commands(void)
{
  struct imv_commands *cmds = imv_commands_create();
  imv_command_register(cmds, "open", capture_command);
  g_assert_cmpint(imv_command_exec(cmds, "open", ""), ==, 0);
  g_assert_cmpint(imv_command_exec(cmds, "  open   file with spaces", "file with spaces"), ==, 0);
  imv_command_alias(cmds, "a", "b");
  imv_command_alias(cmds, "b", "a");
  g_assert_cmpint(imv_command_exec(cmds, "a", NULL), !=, 0);
  imv_commands_free(cmds);
}

static void test_console(void)
{
  struct imv_console *console = imv_console_create();
  imv_console_activate(console);
  char *text = g_strnfill(8192, 'a');
  imv_console_input(console, text);
  g_assert_cmpstr(imv_console_prompt(console), ==, text);
  imv_console_input(console, "й🙂");
  imv_console_key(console, "BackSpace");
  g_assert_cmpuint(strlen(imv_console_prompt(console)), ==, 8194);
  imv_console_key(console, "BackSpace");
  g_assert_cmpstr(imv_console_prompt(console), ==, text);
  imv_console_key(console, "Ctrl+a");
  imv_console_input(console, "я");
  imv_console_key(console, "BackSpace");
  g_assert_cmpstr(imv_console_prompt(console), ==, text);
  g_assert_cmpuint(imv_console_prompt_cursor(console), ==, 0);
  g_free(text);
  imv_console_free(console);
}

static void test_navigation(void)
{
  struct imv_navigator *nav = imv_navigator_create();
  imv_navigator_select_abs(nav, -1);
  g_assert_cmpuint(imv_navigator_index(nav), ==, 0);
  imv_navigator_add(nav, "missing-a", 0);
  imv_navigator_add(nav, "missing-b", 0);
  imv_navigator_add(nav, "missing-c", 0);
  imv_navigator_select_abs(nav, 2);
  imv_navigator_poll_changed(nav);
  imv_navigator_remove_at(nav, 0);
  g_assert_cmpstr(imv_navigator_selection(nav), ==, "missing-c");
  g_assert_cmpuint(imv_navigator_index(nav), ==, 1);
  imv_navigator_poll_changed(nav);
  imv_navigator_select_rel(nav, -1);
  imv_navigator_select_rel(nav, 2);
  g_assert_true(imv_navigator_has_pending_change(nav));
  g_assert_cmpstr(imv_navigator_selection(nav), ==, "missing-b");
  imv_navigator_select_rel(nav, SSIZE_MAX);
  g_assert_cmpstr(imv_navigator_selection(nav), ==, "missing-c");
  imv_navigator_select_rel(nav, -SSIZE_MAX - 1);
  g_assert_cmpstr(imv_navigator_selection(nav), ==, "missing-c");
  imv_navigator_remove_all(nav);
  imv_navigator_select_abs(nav, 999);
  g_assert_cmpuint(imv_navigator_index(nav), ==, 0);
  imv_navigator_free(nav);
}

static void test_directory(void)
{
  char *root = g_dir_make_tmp("imv-nav-audit-XXXXXX", NULL);
  char *file = g_build_filename(root, "image", NULL);
  char *loop = g_build_filename(root, "loop", NULL);
  char *fifo = g_build_filename(root, "fifo", NULL);
  char *link = g_build_filename(root, "link", NULL);
  g_assert_true(g_file_set_contents(file, "image", -1, NULL));
  g_assert_cmpint(symlink(root, loop), ==, 0);
  g_assert_cmpint(symlink(file, link), ==, 0);
  g_assert_cmpint(mkfifo(fifo, 0600), ==, 0);
  struct imv_navigator *nav = imv_navigator_create();
  g_assert_cmpint(imv_navigator_add(nav, root, 1), ==, 0);
  g_assert_cmpuint(imv_navigator_length(nav), ==, 2);
  g_assert_cmpint(imv_navigator_find_path(nav, link), !=, imv_navigator_find_path(nav, file));
  imv_navigator_free(nav);
  if (geteuid() != 0) {
    g_assert_cmpint(chmod(root, 0000), ==, 0);
    nav = imv_navigator_create();
    g_assert_cmpint(imv_navigator_add(nav, root, 1), !=, 0);
    imv_navigator_free(nav);
    g_assert_cmpint(chmod(root, 0700), ==, 0);
  }
  g_unlink(loop); g_unlink(link); g_unlink(fifo); g_unlink(file);
  g_rmdir(root);
  g_free(loop); g_free(link); g_free(fifo); g_free(file); g_free(root);
}

static void test_empty_view(void)
{
  g_assert_null(imv_bitmap_alloc(0, 100).data);
  g_assert_null(imv_bitmap_alloc(-1, 100).data);
  struct imv_viewport *view = imv_viewport_create(800, 600);
  for (int mode = 0; mode < SCALING_MODE_COUNT; ++mode) {
    imv_viewport_rescale(view, NULL, mode);
    double scale;
    imv_viewport_get_scale(view, &scale);
    g_assert_true(isfinite(scale));
    g_assert_cmpfloat(scale, ==, 1);
  }
  imv_viewport_scale_to_window(view, NULL);
  imv_viewport_crop_to_window(view, NULL);
  imv_viewport_zoom(view, NULL, IMV_ZOOM_KEYBOARD, 0, 0, 1);
  double scale;
  imv_viewport_get_scale(view, &scale);
  g_assert_true(isfinite(scale));
  imv_viewport_free(view);
}

static gpointer push_events(gpointer data)
{
  for (int i = 0; i < 20000; ++i) {
    struct imv_event event = {.type = IMV_EVENT_CUSTOM, .data.custom = GINT_TO_POINTER(i)};
    imv_event_queue_push(data, &event);
  }
  return NULL;
}

static void test_event_queue(void)
{
  struct imv_event_queue *queue = imv_event_queue_create();
  g_assert_nonnull(queue);
  int fd = imv_event_queue_fd(queue);
  g_assert_true(fcntl(fd, F_GETFD) & FD_CLOEXEC);
  /* Exceed pipe capacity before consuming any events. */
  g_thread_join(g_thread_new("event-producer", push_events, queue));
  struct pollfd pfd = {.fd = fd, .events = POLLIN};
  g_assert_cmpint(poll(&pfd, 1, 0), ==, 1);
  struct imv_event event;
  for (int i = 0; i < 20000; ++i) {
    g_assert_true(imv_event_queue_pop(queue, &event));
    g_assert_cmpint(GPOINTER_TO_INT(event.data.custom), ==, i);
  }
  g_assert_false(imv_event_queue_pop(queue, &event));
  GThread *producer = g_thread_new("event-producer", push_events, queue);
  for (int i = 0; i < 20000; ++i) {
    while (!imv_event_queue_pop(queue, &event)) {
      g_assert_cmpint(poll(&pfd, 1, 2000), ==, 1);
    }
    g_assert_cmpint(GPOINTER_TO_INT(event.data.custom), ==, i);
  }
  g_thread_join(producer);
  g_assert_false(imv_event_queue_pop(queue, &event));
  imv_event_queue_free(queue);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/regression/long-join", test_long_join);
  g_test_add_func("/regression/commands", test_commands);
  g_test_add_func("/regression/console", test_console);
  g_test_add_func("/regression/navigation", test_navigation);
  g_test_add_func("/regression/directory", test_directory);
  g_test_add_func("/regression/empty-view", test_empty_view);
  g_test_add_func("/regression/event-queue", test_event_queue);
  g_test_add_func("/regression/print-bindings", test_print_bindings);
  return g_test_run();
}
