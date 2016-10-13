// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_TRACE_H_
#define APPS_NETSTACK_TRACE_H_

#include <threads.h>

#define TRACE_TITLE "netstack: "

#define TRACE_LEVEL_SHIFT 0
#define TRACE_LEVEL_BITS 8
#define TRACE_LEVEL_MASK ((1 << TRACE_LEVEL_BITS) - 1)

#define TRACE_FACIL_SHIFT TRACE_LEVEL_BITS
#define TRACE_FACIL_BITS 8
#define TRACE_FACIL_MASK (((1 << TRACE_FACIL_BITS) - 1) << TRACE_FACIL_SHIFT)

#define TRACE_LEVEL_ERROR 1
#define TRACE_LEVEL_INFO 2
#define TRACE_LEVEL_DEBUG 3
#define TRACE_LEVEL_VDEBUG 4

#define TRACE_FACIL_ALL ((1 << TRACE_FACIL_BITS) - 1)

#define TRACE_FACIL_ALLOC 0x1
#define TRACE_FACIL_NET 0x2
#define TRACE_FACIL_SOCKET 0x4
#define TRACE_FACIL_RW 0x8
#define TRACE_FACIL_PORT 0x10
#define TRACE_FACIL_OTHERS 0x80

extern uint32_t g_trace_level;
extern mtx_t g_trace_lock;

void trace_init(void);
void set_trace_level(uint32_t facility, uint32_t level);

#define trace(facility, level, fmt...)                         \
  do {                                                         \
    if ((g_trace_level & ((facility) << TRACE_FACIL_SHIFT)) && \
        (g_trace_level & TRACE_LEVEL_MASK) >= (level)) {       \
      mtx_lock(&g_trace_lock);                                 \
      printf(TRACE_TITLE fmt);                                 \
      mtx_unlock(&g_trace_lock);                               \
    }                                                          \
  } while (0)

#define error(fmt...) trace(TRACE_FACIL_ALL, TRACE_LEVEL_ERROR, fmt)
#define info(fmt...) trace(TRACE_FACIL_ALL, TRACE_LEVEL_INFO, fmt)
#define debug(fmt...) trace(TRACE_FACIL_OTHERS, TRACE_LEVEL_DEBUG, fmt)
#define vdebug(fmt...) trace(TRACE_FACIL_OTHERS, TRACE_LEVEL_VDEBUG, fmt)

#define debug_always(fmt...) trace(TRACE_FACIL_ALL, TRACE_LEVEL_DEBUG, fmt)
#define debug_alloc(fmt...) trace(TRACE_FACIL_ALLOC, TRACE_LEVEL_DEBUG, fmt)
#define debug_net(fmt...) trace(TRACE_FACIL_NET, TRACE_LEVEL_DEBUG, fmt)
#define debug_socket(fmt...) trace(TRACE_FACIL_SOCKET, TRACE_LEVEL_DEBUG, fmt)
#define debug_rw(fmt...) trace(TRACE_FACIL_RW, TRACE_LEVEL_DEBUG, fmt)
#define debug_port(fmt...) trace(TRACE_FACIL_PORT, TRACE_LEVEL_DEBUG, fmt)

const char* getopname(int op);

#endif  // APPS_NETSTACK_TRACE_H_
