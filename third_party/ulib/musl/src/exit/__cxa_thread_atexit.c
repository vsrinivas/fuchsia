// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _ALL_SOURCE
#include "libc.h"
#include "pthread_impl.h"
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

struct tls_dtor {
    struct tls_dtor* next;
    void (*func)(void*);
    void* arg;
};

void __tls_run_dtors(void) {
    struct tls_dtor *cur;
    thrd_t self = __thrd_current();
    while (self->tls_dtors) {
        cur = self->tls_dtors;
        self->tls_dtors = self->tls_dtors->next;
        cur->func(cur->arg);
        free(cur);
    }
}

int __cxa_thread_atexit_impl(void (*func)(void*), void* arg, void* dso) {
    struct tls_dtor* new_td = malloc(sizeof(struct tls_dtor));
    if (!new_td) {
        return -1;
    }
    new_td->func = func;
    new_td->arg = arg;

    // Prepend function to the list, the thread local destructors have to be
    // called in an order determined by the sequenced-before rule according to
    // C++ standard [basic.start.term].
    thrd_t self = __thrd_current();
    new_td->next = self->tls_dtors;
    self->tls_dtors = new_td;

    return 0;
}
