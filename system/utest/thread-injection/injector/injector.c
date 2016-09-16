// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>
#include <stdio.h>
#include <stdlib.h>

#include "../private.h"

#define HELPER "/boot/bin/thread-injection-injected"

int main(void) {
    mx_handle_t h = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0));
    if (h < 0) {
        fprintf(stderr, "mxio_get_startup_handle: %d\n", h);
        return 1;
    }

    // Read the message from the main test program, so we have
    // its process handle and know where its bits are.
    struct helper_data data;
    uint32_t bytes = sizeof(data);
    mx_handle_t proc;
    uint32_t nhandles = 1;
    mx_status_t status = mx_msgpipe_read(h, &data, &bytes,
                                         &proc, &nhandles, 0);
    if (status < 0) {
        fprintf(stderr, "mx_msgpipe_read: %d\n", h);
        return 1;
    }
    if (bytes != sizeof(data)) {
        fprintf(stderr, "read %u bytes instead of %zu\n", bytes, sizeof(data));
        return 1;
    }
    if (nhandles != 1) {
        fprintf(stderr, "read %u handles instead of 1\n", nhandles);
        return 1;
    }

    // Load up the launchpad with the injected program.  This works just
    // like normal program launching, but it goes into a process that
    // already exists (and has another program running in it!).
    launchpad_t* lp;
    status = launchpad_create_with_process(proc, &lp);
    if (status != NO_ERROR) {
        fprintf(stderr, "launchpad_create_with_process: %d\n", status);
        return 1;
    }

    status = launchpad_elf_load(lp, launchpad_vmo_from_file(HELPER));
    if (status != NO_ERROR) {
        fprintf(stderr, "launchpad_elf_load: %d\n", status);
        return 1;
    }

    // TODO(mcgrathr): Eventually we won't permit loading a second copy of
    // the vDSO into a process.  Instead, we'll have some way to discover
    // the vDSO mapping address and feed this to launchpad.
    status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
    if (status != NO_ERROR) {
        fprintf(stderr, "launchpad_load_vdso: %d\n", status);
        return 1;
    }

    // The injected program gets an argument string telling it an
    // address in its own address space (aka the injectee process).
    char buf[32];
    snprintf(buf, sizeof(buf), "%#" PRIxPTR, (uintptr_t)data.futex_addr);
    const char *argv[] = { HELPER, buf };
    status = launchpad_arguments(lp, 2, argv);
    if (status != NO_ERROR) {
        fprintf(stderr, "launchpad_arguments: %d\n", status);
        return 1;
    }

    // Launch the injected program with a new thread.  It gets as
    // its argument the handle number for its end of its bootstrap pipe,
    // which the main test program told us in the helper_data message.
    // (We're reusing the pipe we got helper_data on as the injected
    // program's bootstrap pipe, though the two uses are unrelated.)
    // Launchpad will send the bootstrap messages on our end of the pipe.
    status = launchpad_start_injected(lp, "injected", h, data.bootstrap);
    mx_handle_close(h);
    if (status != NO_ERROR) {
        fprintf(stderr, "launchpad_start_injected: %d\n", status);
        return 1;
    }

    launchpad_destroy(lp);

    return 0;
}
