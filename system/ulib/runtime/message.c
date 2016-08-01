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

#include <runtime/message.h>

#include <magenta/syscalls.h>
#include <stddef.h>

mx_status_t mxr_message_size(mx_handle_t msg_pipe,
                             uint32_t* nbytes, uint32_t* nhandles) {
    *nbytes = *nhandles = 0;
    mx_status_t status = mx_message_read(
        msg_pipe, NULL, nbytes, NULL, nhandles, 0);
    if (status == ERR_NOT_ENOUGH_BUFFER)
        status = NO_ERROR;
    return status;
}
