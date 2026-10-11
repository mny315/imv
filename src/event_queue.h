#ifndef IMV_EVENT_QUEUE_H
#define IMV_EVENT_QUEUE_H

#include <stdbool.h>

struct imv_event;
struct imv_event_queue;

struct imv_event_queue *imv_event_queue_create(void);
/* Stop all producers and drain event payloads before freeing the queue. */
void imv_event_queue_free(struct imv_event_queue *queue);
int imv_event_queue_fd(struct imv_event_queue *queue);
void imv_event_queue_push(struct imv_event_queue *queue, const struct imv_event *event);
bool imv_event_queue_pop(struct imv_event_queue *queue, struct imv_event *event);

#endif
