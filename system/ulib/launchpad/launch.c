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
#include <launchpad/vmo.h>

mx_handle_t launchpad_launch(const char* name,
                             int argc, const char* const* argv,
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
            status = launchpad_arguments(lp, argc, argv);
        if (status == NO_ERROR)
            status = launchpad_add_handles(lp, hnds_count, handles, ids);
        if (status == NO_ERROR)
            proc = launchpad_start(lp);
    }
    launchpad_destroy(lp);

    return status == NO_ERROR ? proc : status;
}
