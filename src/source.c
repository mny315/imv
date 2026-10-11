#include "source.h"
#include "source_private.h"

#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <glib.h>

struct imv_source {
  int id;

  /* pointers to implementation's functions */
  const struct imv_source_vtable *vtable;

  /* pointer to implementation data */
  void *private;

  /* Attempted to be locked by load_first_frame or load_next_frame.
   * If the mutex can't be locked, the call is aborted.
   * Used to prevent the source from having multiple worker threads at once.
   * Released by the source before calling the message callback with a result.
   */
  pthread_mutex_t busy;

  /* Keep the source alive from scheduling a worker through its callback. */
  pthread_mutex_t workers_mutex;
  pthread_cond_t workers_done;
  size_t workers;

  /* callback function */
  imv_source_callback callback;
  /* callback data */
  void *callback_data;
};

struct imv_source *imv_source_create(const struct imv_source_vtable *vtable, void *private)
{
  static gint next_id;
  struct imv_source *source = calloc(1, sizeof *source);
  source->id = g_atomic_int_add(&next_id, 1);
  source->vtable = vtable;
  source->private = private;
  pthread_mutex_init(&source->busy, NULL);
  pthread_mutex_init(&source->workers_mutex, NULL);
  pthread_cond_init(&source->workers_done, NULL);
  return source;
}

int imv_source_get_id(const struct imv_source *src)
{
  return src->id;
}

bool imv_source_is_busy(struct imv_source *src)
{
  pthread_mutex_lock(&src->workers_mutex);
  bool busy = src->workers != 0;
  pthread_mutex_unlock(&src->workers_mutex);
  return busy;
}

static void worker_finished(struct imv_source *src)
{
  pthread_mutex_lock(&src->workers_mutex);
  --src->workers;
  pthread_cond_signal(&src->workers_done);
  pthread_mutex_unlock(&src->workers_mutex);
}

static void start_worker(struct imv_source *src, void *(*run)(void *))
{
  pthread_mutex_lock(&src->workers_mutex);
  ++src->workers;
  pthread_mutex_unlock(&src->workers_mutex);
  pthread_t thread;
  if (pthread_create(&thread, NULL, run, src) == 0) {
    pthread_detach(thread);
  } else {
    /* Resource exhaustion must not leave the viewer waiting for a reply. */
    run(src);
  }
}

static void *free_thread(void *src)
{
  imv_source_free(src);
  return NULL;
}

void imv_source_async_free(struct imv_source *src)
{
  pthread_t thread;
  if (pthread_create(&thread, NULL, free_thread, src) == 0) {
    pthread_detach(thread);
  } else {
    imv_source_free(src);
  }
}

static void *first_frame_thread(void *src_raw)
{
  struct imv_source *src = src_raw;
  struct imv_source_message msg = {
    .source = src,
    .user_data = src->callback_data
  };
  if(imv_source_load_first_frame(src, &msg.image, &msg.frametime)) {
    src->callback(&msg);
  }
  worker_finished(src);
  return NULL;
}

void imv_source_async_load_first_frame(struct imv_source *src)
{
  start_worker(src, first_frame_thread);
}

static void *next_frame_thread(void *src)
{
  imv_source_load_next_frame(src);
  worker_finished(src);
  return NULL;
}
void imv_source_async_load_next_frame(struct imv_source *src)
{
  start_worker(src, next_frame_thread);
}

void imv_source_free(struct imv_source *src)
{
  pthread_mutex_lock(&src->workers_mutex);
  while (src->workers) {
    pthread_cond_wait(&src->workers_done, &src->workers_mutex);
  }
  pthread_mutex_unlock(&src->workers_mutex);
  pthread_mutex_lock(&src->busy);
  src->vtable->free(src->private);
  pthread_mutex_unlock(&src->busy);
  pthread_mutex_destroy(&src->busy);
  pthread_cond_destroy(&src->workers_done);
  pthread_mutex_destroy(&src->workers_mutex);
  free(src);
}

bool imv_source_load_first_frame(struct imv_source *src, struct imv_image **image, int *frametime)
{
  if (!src->vtable->load_first_frame) {
    return false;
  }

  if (pthread_mutex_trylock(&src->busy)) {
    return false;
  }

  src->vtable->load_first_frame(src->private, image, frametime);

  if(pthread_mutex_unlock(&src->busy)) {
    // We locked the mutex so this can never fail
    assert(false);
  }
  return true;
}

void imv_source_load_next_frame(struct imv_source *src)
{
  if (!src->vtable->load_next_frame) {
    return;
  }

  if (pthread_mutex_trylock(&src->busy)) {
    return;
  }

  struct imv_source_message msg = {
    .source = src,
    .user_data = src->callback_data
  };

  src->vtable->load_next_frame(src->private, &msg.image, &msg.frametime);

  pthread_mutex_unlock(&src->busy);

  src->callback(&msg);
}

void imv_source_set_callback(struct imv_source *src, imv_source_callback callback,
    void *data)
{
  src->callback = callback;
  src->callback_data = data;
}
