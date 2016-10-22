// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "apps/netstack/multiplexer.h"
#include "apps/netstack/request_queue.h"
#include "apps/netstack/trace.h"

static const char* s_mxrio_opnames[] = MXRIO_OPNAMES;
static const char* s_io_opnames[] = IO_OPNAMES;

const char* getopname(int op) {
  if (op >= 0 && op < MXRIO_NUM_OPS) return s_mxrio_opnames[op];
  if (op >= 0 && op < NUM_OPS) return s_io_opnames[op - MXRIO_NUM_OPS];
  return "unknown";
}

request_t* request_pack(int op, mx_handle_t rh, mxrio_msg_t* msg,
                        iostate_t* ios) {
  request_t* rq = calloc(1, sizeof(request_t));
  debug_alloc("request_pack: alloc rq %p\n", rq);
  assert(rq);

  rq->next = NULL;
  rq->op = op;
  rq->rh = rh;
  rq->msg = msg;
  rq->ios = ios;

  return rq;
}

void request_free(request_t* rq) { free(rq); }

void request_unpack(request_t* rq, int* op, mx_handle_t* rh, mxrio_msg_t** msg,
                    iostate_t** ios) {
  if (op != NULL) *op = rq->op;
  if (rh != NULL) *rh = rq->rh;
  if (msg != NULL) *msg = rq->msg;
  if (ios != NULL) *ios = rq->ios;
}

void request_queue_init(request_queue_t* q) {
  q->head = NULL;
  q->tail = NULL;
}

void request_queue_swap(request_queue_t* q1, request_queue_t* q2) {
  request_queue_t temp;
  temp = *q1;
  *q1 = *q2;
  *q2 = temp;
}

void request_queue_put(request_queue_t* q, request_t* rq) {
  if (q->head == NULL) {
    // the first entry
    q->head = rq;
    q->tail = rq;
  } else {
    // not the first entry
    q->tail->next = rq;
    q->tail = rq;
  }
  rq->next = NULL;
}

request_t* request_queue_get(request_queue_t* q) {
  request_t* rq = q->head;
  if (rq) {
    if (rq == q->tail) {
      // the last entry
      q->head = NULL;
      q->tail = NULL;
    } else {
      // not the last entry
      q->head = rq->next;
    }
    rq->next = NULL;
  }
  return rq;
}

void request_queue_discard(request_queue_t* q) {
  request_t* rq = q->head;
  while (rq) {
    request_t* next = rq->next;
    debug_alloc("request_queue_discard: request_free rq %p\n", rq);
    request_free(rq);
    rq = next;
  }
  q->head = NULL;
  q->tail = NULL;
}

// wait queue

static request_queue_t wait_queue[2][NSOCKETS];

void wait_queue_swap(int type, int sockfd, request_queue_t* q) {
  assert(sockfd >= 0 && sockfd < NSOCKETS);
  request_queue_swap(&wait_queue[type][sockfd], q);
}

void wait_queue_put(int type, int sockfd, request_t* rq) {
  assert(sockfd >= 0 && sockfd < NSOCKETS);
  request_queue_put(&wait_queue[type][sockfd], rq);
}

void wait_queue_discard(int type, int sockfd) {
  assert(sockfd >= 0 && sockfd < NSOCKETS);
  request_queue_discard(&wait_queue[type][sockfd]);
}

// shared request queue

static int s_readfd = -1;
static int s_writefd = -1;
static mtx_t s_lock;
static request_queue_t shared_queue;

mx_status_t shared_queue_create(void) {
  mtx_init(&s_lock, mtx_plain);
  request_queue_init(&shared_queue);

  return interrupter_create(&s_writefd, &s_readfd);
}

mx_status_t shared_queue_put(request_t* rq) {
  mtx_lock(&s_lock);
  request_queue_put(&shared_queue, rq);
  mx_status_t r = send_interrupt(s_writefd);
  mtx_unlock(&s_lock);
  return r;
}

request_t* shared_queue_get(void) {
  mtx_lock(&s_lock);
  request_t* rq = request_queue_get(&shared_queue);
  if (rq == NULL) {
    mtx_unlock(&s_lock);
    return NULL;
  }
  clear_interrupt(s_readfd);
  mtx_unlock(&s_lock);
  return rq;
}

mx_status_t shared_queue_pack_and_put(int op, mx_handle_t rh, mxrio_msg_t* msg,
                                      iostate_t* ios) {
  return shared_queue_put(request_pack(op, rh, msg, ios));
}

int shared_queue_readfd(void) { return s_readfd; }

int shared_queue_writefd(void) { return s_writefd; }
