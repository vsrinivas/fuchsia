#pragma once

#include <mxu/list.h>
#include <stdint.h>

typedef void poll_cb(void* context);

typedef struct poll_node {
    list_node_t node;
    poll_cb* cb;
    void* context;
} poll_node_t;

void poll_add(poll_node_t* node, poll_cb* db, void* context);
void poll_remove(poll_node_t* node);

void usb_poll_start(void);
