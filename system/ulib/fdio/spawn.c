// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/spawn.h>

#include <fuchsia/process/c/fidl.h>

zx_status_t fdio_spawn(zx_handle_t job,
                       uint32_t flags,
                       const char* path,
                       const char* const* argv,
                       zx_handle_t* process_out) {
    return fdio_spawn_etc(job, flags, path, argv, NULL, 0, NULL, process_out, NULL);
}

zx_status_t fdio_spawn_etc(zx_handle_t job,
                           uint32_t flags,
                           const char* path,
                           const char* const* argv,
                           const char* const* environ,
                           size_t action_count,
                           const fdio_spawn_action_t* actions,
                           zx_handle_t* process_out,
                           char* err_msg_out) {
    return ZX_ERR_NOT_SUPPORTED;
}
