// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_REQUEST_QUEUE_H_
#define APPS_NETSTACK_REQUEST_QUEUE_H_

#include <magenta/types.h>
#include <mxio/remoteio.h>

#include "apps/netstack/iostate.h"

#define NSOCKETS 64

#define IO_SIGCONN_R (MXRIO_NUM_OPS + 0)
#define IO_SIGCONN_W (MXRIO_NUM_OPS + 1)
#define NUM_OPS (MXRIO_NUM_OPS + 2)

#define IO_OPNAMES \
  { "sigconn_r", "sigconn_w" }

const char* getopname(int op);

typedef struct request {
  struct request* next;
  int op;
  mx_handle_t rh;
  mxrio_msg_t* msg;
  iostate_t* ios;
} request_t;

request_t* request_pack(int op, mx_handle_t rh, mxrio_msg_t* msg,
                        iostate_t* ios);

void request_free(request_t* rq);

void request_unpack(request_t* rq, int* op, mx_handle_t* rh, mxrio_msg_t** msg,
                    iostate_t** ios);

typedef struct request_queue {
  request_t* head;
  request_t* tail;
} request_queue_t;

void request_queue_init(request_queue_t* q);
void request_queue_swap(request_queue_t* q1, request_queue_t* q2);
void request_queue_put(request_queue_t* q, request_t* rq);
request_t* request_queue_get(request_queue_t* q);
void request_queue_discard(request_queue_t* q);

#define WAIT_NET 0
#define WAIT_SOCKET 1

void wait_queue_swap(int type, int sockfd, request_queue_t* q);
void wait_queue_put(int type, int sockfd, request_t* rq);
void wait_queue_discard(int type, int sockfd);

mx_status_t shared_queue_create(void);
mx_status_t shared_queue_put(request_t* rq);
request_t* shared_queue_get(void);

mx_status_t shared_queue_pack_and_put(int op, mx_handle_t rh, mxrio_msg_t* msg,
                                      iostate_t* ios);

int shared_queue_readfd(void);
int shared_queue_writefd(void);

#endif  // APPS_NETSTACK_REQUEST_QUEUE_H_
