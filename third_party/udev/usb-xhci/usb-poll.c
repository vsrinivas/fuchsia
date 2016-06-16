#include <pthread.h>
#include <unistd.h>

#include "usb-poll.h"

static list_node_t poll_list = LIST_INITIAL_VALUE(poll_list);
static bool started = false;

void poll_add(poll_node_t* node, poll_cb* cb, void* context) {
    node->cb = cb;
    node->context = context;
    list_add_head(&poll_list, &node->node);
}
void poll_remove(poll_node_t* node) {
    list_delete(&node->node);
}

static void* usb_poll_thread(void* arg) {
    while (1) {
        poll_node_t* poll;
        poll_node_t* temp;
        list_for_every_entry_safe (&poll_list, poll, temp, poll_node_t, node) {
            poll->cb(poll->context);
        }
        usleep(1000);
    }
    return NULL;
}

void usb_poll_start(void) {
    if (!started) {
        pthread_t tid;
        pthread_create(&tid, NULL, usb_poll_thread, NULL);
        started = true;
    }
}
