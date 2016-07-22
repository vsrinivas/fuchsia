// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <launchpad/launchpad.h>

#include <magenta/syscalls.h>
#include <mxio/util.h>

static mx_status_t add_mxio(launchpad_t* lp,
                            mx_handle_t handles[MXIO_MAX_HANDLES],
                            uint32_t types[MXIO_MAX_HANDLES],
                            mx_status_t status) {
    if (status > 0) {
        size_t n = status;
        status = launchpad_add_handles(lp, n, handles, types);
        if (status != NO_ERROR) {
            for (size_t i = 0; i < n; ++i)
                mx_handle_close(handles[i]);
        }
    }
    return status;
}


mx_status_t launchpad_clone_mxio_root(launchpad_t* lp) {
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    return add_mxio(lp, handles, types, mxio_clone_root(handles, types));
}

mx_status_t launchpad_clone_fd(launchpad_t* lp, int fd, int target_fd) {
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    return add_mxio(lp, handles, types,
                    mxio_clone_fd(fd, target_fd, handles, types));
}
