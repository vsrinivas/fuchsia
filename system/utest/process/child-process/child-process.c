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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

static mx_status_t my_read_message(mx_handle_t handle, void* bytes, uint32_t* num_bytes,
                                   mx_handle_t* handles, uint32_t* num_handles, uint32_t flags) {
    return mx_message_read(handle, bytes, num_bytes, handles, num_handles, flags);
}

static mx_status_t my_write_message(mx_handle_t handle, const void* bytes, uint32_t num_bytes,
                                    const mx_handle_t* handles, uint32_t num_handles,
                                    uint32_t flags) {
    return mx_message_write(handle, bytes, num_bytes, handles, num_handles, flags);
}

static mx_status_t my_wait(const mx_handle_t* handles, const mx_signals_t* signals,
                           uint32_t num_handles, uint32_t* result_index,
                           mx_time_t deadline,
                           mx_signals_t* satisfied_signals,
                           mx_signals_t* satisfiable_signals) {
    mx_status_t result;

    if (num_handles == 1u) {
        result =
            mx_handle_wait_one(*handles, *signals, MX_TIME_INFINITE,
                                     satisfied_signals, satisfiable_signals);
    } else {
        result = mx_handle_wait_many(num_handles, handles, signals, MX_TIME_INFINITE,
                                           satisfied_signals, satisfiable_signals);
    }

    // from _magenta_wait*: TODO(cpu): implement |result_index|, see MG-33 bug.
    return result;
}

static bool wait_readable(mx_handle_t handle) {
    mx_signals_t satisfied_signals, satisfiable_signals;
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    mx_status_t result = my_wait(&handle, &signals, 1, NULL, MX_TIME_INFINITE,
                                 &satisfied_signals, &satisfiable_signals);
    if (result != NO_ERROR) {
        unittest_printf("child-test: my_wait returned %d\n", result);
        return false;
    } else if ((satisfied_signals & MX_SIGNAL_READABLE) == 0) {
        unittest_printf("child-test: my_wait peer closed\n");
        return false;
    }
    return true;
}

static void* arg;

void* __libc_intercept_arg(void* _arg) {
    arg = _arg;
    return NULL;
}

int main(int argc, char** argv) {
    mx_handle_t handle = (mx_handle_t)(intptr_t)arg;
    unittest_printf("child-process: got arg %u\n", handle);

    if (!wait_readable(handle))
        return -1;

    char buffer[64];
    uint32_t buffer_size = sizeof(buffer);
    memset(buffer, 0, sizeof(buffer));
    mx_status_t status = my_read_message(handle, buffer, &buffer_size, NULL, NULL, 0);
    unittest_printf("child-process: my_read_message returned %d\n", status);
    unittest_printf("child-process: received \"%s\"\n", buffer);

    unittest_printf("child-process: sleeping a bit before responding\n");
    mx_nanosleep(200 * 1000 * 1000);

    buffer_size = sizeof(buffer);
    snprintf(buffer, buffer_size, "Hi there to you too!");

    status = my_write_message(handle, buffer, strlen(buffer) + 1, NULL, 0, 0);
    unittest_printf("child-process: my_write_message returned %d\n", status);

    unittest_printf("child-process: done\n");

    return 1234;
}
