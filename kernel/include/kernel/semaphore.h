// Copyright 2016 The Fuchsia Authors
// Copyright 2012 Christopher Anderson <chris@nullcode.org>
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#ifndef __KERNEL_SEMAPHORE_H
#define __KERNEL_SEMAPHORE_H

#include <magenta/compiler.h>
#include <kernel/thread.h>
#include <kernel/mutex.h>

__BEGIN_CDECLS;

#define SEMAPHORE_MAGIC (0x73656D61) // 'sema'

typedef struct semaphore {
    int magic;
    int count;
    wait_queue_t wait;
} semaphore_t;

#define SEMAPHORE_INITIAL_VALUE(s, _count) \
{ \
    .magic = SEMAPHORE_MAGIC, \
    .count = _count, \
    .wait = WAIT_QUEUE_INITIAL_VALUE((s).wait), \
}

void sem_init(semaphore_t *, unsigned int);
void sem_destroy(semaphore_t *);
int sem_post(semaphore_t *, bool resched);
status_t sem_wait(semaphore_t *);
status_t sem_trywait(semaphore_t *);
status_t sem_timedwait(semaphore_t *, lk_time_t);

__END_CDECLS;
#endif
