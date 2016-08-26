// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <magenta/compiler.h>
#include <list.h>
#include <sys/types.h>

__BEGIN_CDECLS

struct dpc;
typedef void (*dpc_func_t)(struct dpc *);

typedef struct dpc {
    struct list_node node;

    dpc_func_t func;
    void *arg;
} dpc_t;

status_t dpc_queue(dpc_t *dpc, bool reschedule);

__END_CDECLS

