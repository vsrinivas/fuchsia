// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxio/remoteio.h>

#include "apps/netstack/trace.h"

uint32_t g_trace_level;
mtx_t g_trace_lock;

void trace_init() { mtx_init(&g_trace_lock, mtx_plain); }

void set_trace_level(uint32_t facility, uint32_t level) {
  g_trace_level = ((facility << TRACE_FACIL_SHIFT) & TRACE_FACIL_MASK) |
                  ((level << TRACE_LEVEL_SHIFT) & TRACE_LEVEL_MASK);
}

static const char* s_opnames[] = MXRIO_OPNAMES;

const char* getopname(int op) {
  if (op >= MXRIO_NUM_OPS) return "unknown";
  return s_opnames[op];
}
