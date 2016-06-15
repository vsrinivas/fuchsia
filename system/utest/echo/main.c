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

#include <assert.h>
#include <stdio.h>

#include <magenta/syscalls.h>

#include "echo.h"

int main(void) {
    mx_handle_t handles[2] = {0};
    handles[0] = _magenta_message_pipe_create(&handles[1]);
    if (handles[0] < 0) {
        printf("could not create message pipe: %u\n", handles[0]);
        return 1;
    }
    printf("created message pipe with handle values %u and %u\n", handles[0], handles[1]);
    for (int i = 0; i < 3; i++) {
        printf("loop %d\n", i);
        static const uint32_t buf[9] = {
            24,         // struct header, num_bytes
            1,          // struct header: version
            0,          // struct header: flags
            1,          // message header: name
            0, 0,       // message header: request id (8 bytes)
            4,          // array header: num bytes
            4,          // array header: num elems
            0x42424143, // array contents: 'CABB'
        };
        mx_handle_t status = _magenta_message_write(handles[1], (void*)buf, sizeof(buf), NULL, 0u, 0u);
        if (status != NO_ERROR) {
            printf("could not write echo request: %u\n", status);
            return 1;
        }

        if (!serve_echo_request(handles[0])) {
            printf("serve_echo_request failed\n");
            break;
        }
    }
    printf("closing handle %u\n", handles[1]);
    _magenta_handle_close(handles[1]);
    serve_echo_request(handles[0]);
    _magenta_handle_close(handles[0]);
    return 0;
}
