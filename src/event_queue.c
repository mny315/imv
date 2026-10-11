#include "event_queue.h"
#include "window.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <unistd.h>

struct imv_event_queue {
  GAsyncQueue *events;
  int pipe_fds[2];
};

struct imv_event_queue *imv_event_queue_create(void)
{
  struct imv_event_queue *queue = g_new0(struct imv_event_queue, 1);
  if (pipe(queue->pipe_fds) != 0) {
    g_free(queue);
    return NULL;
  }
  for (int i = 0; i < 2; ++i) {
    int fd = queue->pipe_fds[i];
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
      close(queue->pipe_fds[0]);
      close(queue->pipe_fds[1]);
      g_free(queue);
      return NULL;
    }
  }
  queue->events = g_async_queue_new_full(g_free);
  return queue;
}

void imv_event_queue_free(struct imv_event_queue *queue)
{
  if (!queue) return;
  close(queue->pipe_fds[0]);
  close(queue->pipe_fds[1]);
  g_async_queue_unref(queue->events);
  g_free(queue);
}

int imv_event_queue_fd(struct imv_event_queue *queue)
{
  return queue->pipe_fds[0];
}

void imv_event_queue_push(struct imv_event_queue *queue, const struct imv_event *event)
{
  struct imv_event *copy = g_new(struct imv_event, 1);
  *copy = *event;
  g_async_queue_push(queue->events, copy);
  /* The pipe only wakes poll(). A full pipe is already readable; the event
   * itself remains in the queue, even when this write returns EAGAIN. */
  while (write(queue->pipe_fds[1], "", 1) == -1 && errno == EINTR) {}
}

bool imv_event_queue_pop(struct imv_event_queue *queue, struct imv_event *event)
{
  char wakeups[256];
  while (1) {
    ssize_t count = read(queue->pipe_fds[0], wakeups, sizeof wakeups);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
  }
  struct imv_event *next = g_async_queue_try_pop(queue->events);
  if (!next) return false;
  *event = *next;
  g_free(next);
  return true;
}
