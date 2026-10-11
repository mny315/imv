#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_COMMAND_LENGTH 65536

struct imv_ipc {
  int fd;
  int stop_pipe[2];
  char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
  pthread_t thread;
  pthread_mutex_t callback_mutex;
  imv_ipc_callback callback;
  void *data;
};

struct connection {
  int fd;
  GString *command;
  bool oversized;
};

static void free_connection(void *data)
{
  struct connection *conn = data;
  close(conn->fd);
  g_string_free(conn->command, true);
  free(conn);
}

static void dispatch_command(struct imv_ipc *ipc, struct connection *conn)
{
  if (!conn->oversized && conn->command->len) {
    g_strchomp(conn->command->str);
    pthread_mutex_lock(&ipc->callback_mutex);
    imv_ipc_callback callback = ipc->callback;
    void *data = ipc->data;
    pthread_mutex_unlock(&ipc->callback_mutex);
    if (callback && conn->command->str[0]) {
      callback(conn->command->str, data);
    }
  }
  g_string_truncate(conn->command, 0);
  conn->oversized = false;
}

static bool read_commands(struct imv_ipc *ipc, struct connection *conn)
{
  char buf[4096];
  ssize_t len = recv(conn->fd, buf, sizeof buf, 0);
  if (len < 0) return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
  if (len == 0) {
    dispatch_command(ipc, conn);
    return false;
  }
  for (ssize_t i = 0; i < len; ++i) {
    if (buf[i] == '\n') {
      dispatch_command(ipc, conn);
    } else if (!conn->oversized) {
      if (conn->command->len >= MAX_COMMAND_LENGTH || buf[i] == '\0') {
        conn->oversized = true;
        g_string_truncate(conn->command, 0);
      } else {
        g_string_append_c(conn->command, buf[i]);
      }
    }
  }
  return true;
}

static bool prepare_fd(int fd)
{
  return fcntl(fd, F_SETFD, FD_CLOEXEC) != -1 &&
    fcntl(fd, F_SETFL, O_NONBLOCK) != -1;
}

static void *wait_for_commands(void *data)
{
  struct imv_ipc *ipc = data;
  GPtrArray *connections = g_ptr_array_new_with_free_func(free_connection);
  while (1) {
    size_t count = connections->len;
    struct pollfd *fds = g_new0(struct pollfd, count + 2);
    fds[0] = (struct pollfd){.fd = ipc->stop_pipe[0], .events = POLLIN};
    fds[1] = (struct pollfd){.fd = ipc->fd, .events = POLLIN};
    for (size_t i = 0; i < count; ++i) {
      struct connection *conn = connections->pdata[i];
      fds[i + 2] = (struct pollfd){.fd = conn->fd, .events = POLLIN};
    }
    int result = poll(fds, count + 2, -1);
    if ((result < 0 && errno != EINTR) || fds[0].revents) {
      g_free(fds);
      break;
    }
    for (size_t i = count; i-- > 0;) {
      if (fds[i + 2].revents && !read_commands(ipc, connections->pdata[i])) {
        g_ptr_array_remove_index(connections, i);
      }
    }
    if (fds[1].revents & POLLIN) {
      int client = accept(ipc->fd, NULL, NULL);
      if (client != -1) {
        if (connections->len >= 64 || !prepare_fd(client)) {
          close(client);
        } else {
          struct connection *conn = g_new0(struct connection, 1);
          conn->fd = client;
          conn->command = g_string_new(NULL);
          g_ptr_array_add(connections, conn);
        }
      }
    }
    g_free(fds);
  }
  g_ptr_array_unref(connections);
  return NULL;
}

struct imv_ipc *imv_ipc_create(void)
{
  struct imv_ipc *ipc = g_new0(struct imv_ipc, 1);
  ipc->fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (ipc->fd < 0 || !prepare_fd(ipc->fd)) {
    if (ipc->fd >= 0) close(ipc->fd);
    free(ipc);
    return NULL;
  }
  struct sockaddr_un desc = {.sun_family = AF_UNIX};
  imv_ipc_path(ipc->path, sizeof ipc->path, getpid());
  memcpy(desc.sun_path, ipc->path, sizeof desc.sun_path);

  /* A stale socket may remain after a crash. Never unlink another user's
   * socket, a regular file, a symlink, or an active listener. */
  struct stat st;
  if (lstat(ipc->path, &st) == 0) {
    if (!S_ISSOCK(st.st_mode) || st.st_uid != getuid() ||
        connect(ipc->fd, (struct sockaddr *)&desc, sizeof desc) == 0 ||
        errno != ECONNREFUSED) {
      close(ipc->fd);
      free(ipc);
      return NULL;
    }
    unlink(ipc->path);
  }
  if (!prepare_fd(ipc->fd) || bind(ipc->fd, (struct sockaddr *)&desc, sizeof desc)) {
    close(ipc->fd);
    free(ipc);
    return NULL;
  }
  if (chmod(ipc->path, 0600) || listen(ipc->fd, 5) || pipe(ipc->stop_pipe)) {
    unlink(ipc->path);
    close(ipc->fd);
    free(ipc);
    return NULL;
  }
  if (!prepare_fd(ipc->stop_pipe[0]) || !prepare_fd(ipc->stop_pipe[1])) {
    close(ipc->stop_pipe[0]);
    close(ipc->stop_pipe[1]);
    unlink(ipc->path);
    close(ipc->fd);
    free(ipc);
    return NULL;
  }
  pthread_mutex_init(&ipc->callback_mutex, NULL);
  if (pthread_create(&ipc->thread, NULL, wait_for_commands, ipc)) {
    pthread_mutex_destroy(&ipc->callback_mutex);
    close(ipc->stop_pipe[0]);
    close(ipc->stop_pipe[1]);
    unlink(ipc->path);
    close(ipc->fd);
    free(ipc);
    return NULL;
  }
  return ipc;
}

void imv_ipc_free(struct imv_ipc *ipc)
{
  if (!ipc) return;
  while (write(ipc->stop_pipe[1], "", 1) == -1 && errno == EINTR) {}
  pthread_join(ipc->thread, NULL);
  close(ipc->stop_pipe[0]);
  close(ipc->stop_pipe[1]);
  close(ipc->fd);
  unlink(ipc->path);
  pthread_mutex_destroy(&ipc->callback_mutex);
  free(ipc);
}

void imv_ipc_set_command_callback(struct imv_ipc *ipc,
    imv_ipc_callback callback, void *data)
{
  pthread_mutex_lock(&ipc->callback_mutex);
  ipc->callback = callback;
  ipc->data = data;
  pthread_mutex_unlock(&ipc->callback_mutex);
}
