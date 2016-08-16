// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

mx_handle_t launchpad_launch(const char* name,
                             int argc, const char* const* argv,
                             const char* const* envp,
                             size_t hnds_count, mx_handle_t* handles,
                             uint32_t* ids) {
    launchpad_t* lp;

    const char* filename = argv[0];
    if (name == NULL)
        name = filename;

    mx_handle_t proc = MX_HANDLE_INVALID;
    mx_status_t status = launchpad_create(name, &lp);
    if (status == NO_ERROR) {
        status = launchpad_elf_load(lp, launchpad_vmo_from_file(filename));
        if (status == NO_ERROR)
            status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
        if (status == NO_ERROR)
            status = launchpad_arguments(lp, argc, argv);
        if (status == NO_ERROR)
            status = launchpad_environ(lp, envp);
        if (status == NO_ERROR)
            status = launchpad_add_handles(lp, hnds_count, handles, ids);
        if (status == NO_ERROR)
            proc = launchpad_start(lp);
    }
    launchpad_destroy(lp);

    return status == NO_ERROR ? proc : status;
}
