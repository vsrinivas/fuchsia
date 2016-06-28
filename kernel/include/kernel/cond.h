// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __KERNEL_COND_H
#define __KERNEL_COND_H

#include <compiler.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <sys/types.h>

__BEGIN_CDECLS;

#define COND_MAGIC (0x636f6e64)  // "cond"

typedef struct cond {
    uint32_t magic;
    wait_queue_t wait;
} cond_t;

#define COND_INITIAL_VALUE(cond) \
{ \
    .magic = COND_MAGIC, \
    .wait = WAIT_QUEUE_INITIAL_VALUE((cond).wait), \
}

void cond_init(cond_t *cond);
void cond_destroy(cond_t *cond);
status_t cond_wait_timeout(cond_t *cond, mutex_t *mutex, lk_time_t timeout);
void cond_signal(cond_t *cond);
void cond_broadcast(cond_t *cond);

__END_CDECLS;

#endif
