// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <magenta/syscalls.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "../private.h"

// This program appears to start normally, but is actually running in the
// same process as another program that's already running.

int main(int argc, char** argv) {
    // The injector starts this here program with an argument that is an
    // address in the injectee program's process (in which this here
    // program is also running).
    if (argc != 2)
        abort();
    uintptr_t addr = strtoul(argv[1], NULL, 0);
    atomic_int* my_futex = (atomic_int*)addr;

    // The main test program (i.e. the original resident of this here
    // process) is waiting on this futex.  Wake it up with a value it's
    // looking for.  When it sees this value arrive, the test succeeds.
    atomic_store(my_futex, MAGIC);
    mx_status_t status = mx_futex_wake(my_futex, -1);
    if (status != MX_OK)
        abort();

    // If we return, that will call exit and kill the whole process.
    // Just exit this thread instead.
    mx_thread_exit();
}
