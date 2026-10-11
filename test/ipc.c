/* Exercise stream framing without requiring a display or a bound socket. */
#include "../src/ipc.c"

#include <glib/gstdio.h>

static void capture(const char *command, void *data)
{
  g_ptr_array_add(data, g_strdup(command));
}

static void test_framing(void)
{
  int pair[2];
  g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
  GPtrArray *received = g_ptr_array_new_with_free_func(g_free);
  struct imv_ipc ipc = {0};
  pthread_mutex_init(&ipc.callback_mutex, NULL);
  imv_ipc_set_command_callback(&ipc, capture, received);
  struct connection conn = {.fd = pair[0], .command = g_string_new(NULL)};
  g_assert_cmpint(write(pair[1], "ne", 2), ==, 2);
  g_assert_true(read_commands(&ipc, &conn));
  g_assert_cmpuint(received->len, ==, 0);
  g_assert_cmpint(write(pair[1], "xt\nprev\nlast", 12), ==, 12);
  g_assert_true(read_commands(&ipc, &conn));
  g_assert_cmpuint(received->len, ==, 2);
  g_assert_cmpstr(received->pdata[0], ==, "next");
  g_assert_cmpstr(received->pdata[1], ==, "prev");
  close(pair[1]);
  g_assert_false(read_commands(&ipc, &conn));
  g_assert_cmpuint(received->len, ==, 3);
  g_assert_cmpstr(received->pdata[2], ==, "last");
  g_string_free(conn.command, true);
  close(pair[0]);
  pthread_mutex_destroy(&ipc.callback_mutex);
  g_ptr_array_unref(received);
}

static void test_long_command(void)
{
  int pair[2];
  g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
  GPtrArray *received = g_ptr_array_new_with_free_func(g_free);
  struct imv_ipc ipc = {0};
  pthread_mutex_init(&ipc.callback_mutex, NULL);
  imv_ipc_set_command_callback(&ipc, capture, received);
  struct connection conn = {.fd = pair[0], .command = g_string_new(NULL)};
  char bytes[4096];
  memset(bytes, 'x', sizeof bytes);
  for (int i = 0; i < 20; ++i) {
    g_assert_cmpint(write(pair[1], bytes, sizeof bytes), ==, sizeof bytes);
    g_assert_true(read_commands(&ipc, &conn));
  }
  g_assert_cmpuint(received->len, ==, 0);
  g_assert_cmpint(write(pair[1], "\nnext\n", 6), ==, 6);
  g_assert_true(read_commands(&ipc, &conn));
  g_assert_cmpuint(received->len, ==, 1);
  g_assert_cmpstr(received->pdata[0], ==, "next");
  /* A legitimate command longer than one recv buffer must remain intact. */
  for (int i = 0; i < 3; ++i) {
    g_assert_cmpint(write(pair[1], bytes, sizeof bytes), ==, sizeof bytes);
    g_assert_true(read_commands(&ipc, &conn));
  }
  g_assert_cmpint(write(pair[1], "\n", 1), ==, 1);
  g_assert_true(read_commands(&ipc, &conn));
  g_assert_cmpuint(received->len, ==, 2);
  g_assert_cmpuint(strlen(received->pdata[1]), ==, sizeof bytes * 3);
  close(pair[0]); close(pair[1]);
  g_string_free(conn.command, true);
  pthread_mutex_destroy(&ipc.callback_mutex);
  g_ptr_array_unref(received);
}

static void test_shutdown(void)
{
  /* Start the real polling worker with an idle socketpair as its listener.
   * This also runs where a sandbox forbids binding named UNIX sockets. */
  int pair[2];
  g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
  struct imv_ipc *ipc = g_new0(struct imv_ipc, 1);
  ipc->fd = pair[0];
  g_assert_cmpint(pipe(ipc->stop_pipe), ==, 0);
  pthread_mutex_init(&ipc->callback_mutex, NULL);
  g_assert_cmpint(pthread_create(&ipc->thread, NULL, wait_for_commands, ipc), ==, 0);
  imv_ipc_free(ipc);
  close(pair[1]);
}

static void test_server(void)
{
  char *root = g_dir_make_tmp("imv-ipc-audit-XXXXXX", NULL);
  g_setenv("XDG_RUNTIME_DIR", root, true);
  struct imv_ipc *ipc = imv_ipc_create();
  if (!ipc) {
    if (errno == EPERM || errno == EACCES) {
      g_test_skip("Sandbox forbids binding UNIX sockets");
      g_rmdir(root); g_free(root);
      return;
    }
    g_assert_nonnull(ipc);
  }
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  imv_ipc_path(addr.sun_path, sizeof addr.sun_path, getpid());
  g_assert_cmpint(connect(fd, (struct sockaddr *)&addr, sizeof addr), ==, 0);
  g_assert_cmpint(write(fd, "incomplete", 10), ==, 10);
  imv_ipc_free(ipc);
  close(fd);
  g_assert_false(g_file_test(addr.sun_path, G_FILE_TEST_EXISTS));
  g_rmdir(root); g_free(root);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/ipc/framing", test_framing);
  g_test_add_func("/ipc/long-command", test_long_command);
  g_test_add_func("/ipc/shutdown", test_shutdown);
  g_test_add_func("/ipc/server", test_server);
  return g_test_run();
}
