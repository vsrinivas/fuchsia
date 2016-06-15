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

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mxio/util.h>
#include <magenta/syscalls.h>

static mx_handle_t my_process_create(const char *name, uint32_t name_len) {
    return _magenta_process_create(name, name_len);
}

static mx_status_t my_process_load(mx_handle_t process, const char* elf_file, uintptr_t* out_entry) {
    int fd = open(elf_file, O_RDONLY);
    if (fd < 0) return ERR_IO;

    uintptr_t entry = 0;
    mx_status_t status = mxio_load_elf_fd(process, &entry, fd);
    close(fd);
    if (status < 0)
        return status;

    *out_entry = entry;
    return NO_ERROR;
}

static mx_status_t my_process_start(mx_handle_t process, mx_handle_t handle, uintptr_t entry) {
    return _magenta_process_start(process, handle, entry);
}

static mx_status_t my_wait(const mx_handle_t* handles, const mx_signals_t* signals,
                           uint32_t num_handles, uint32_t* result_index,
                           mx_time_t deadline,
                           mx_signals_t* satisfied_signals,
                           mx_signals_t* satisfiable_signals)
{
    mx_status_t result;

    if (num_handles == 1u) {
        result =
            _magenta_handle_wait_one(*handles, *signals, MX_TIME_INFINITE,
                                     satisfied_signals, satisfiable_signals);
    } else {
        result = _magenta_handle_wait_many(num_handles, handles, signals, MX_TIME_INFINITE,
                                           satisfied_signals, satisfiable_signals);
    }

    // from _magenta_wait_*: TODO(cpu): implement |result_index|, see MG-33 bug.
    return result;
}

static mx_status_t my_create_message_pipe(mx_handle_t* handle0, mx_handle_t* handle1)
{
    mx_handle_t result = _magenta_message_pipe_create(handle1);
    if (result < 0)
        return result;
    *handle0 = result;
    return NO_ERROR;
}

static mx_status_t my_read_message(mx_handle_t handle, void* bytes, uint32_t* num_bytes,
                                   mx_handle_t* handles, uint32_t* num_handles, uint32_t flags)
{
    return _magenta_message_read(handle, bytes, num_bytes, handles, num_handles, flags);
}

static mx_status_t my_write_message(mx_handle_t handle, const void* bytes, uint32_t num_bytes,
                                    const mx_handle_t* handles, uint32_t num_handles,
                                    uint32_t flags)
{
    return _magenta_message_write(handle, bytes, num_bytes, handles, num_handles, flags);
}

static mx_status_t my_close(mx_handle_t handle)
{
    return _magenta_handle_close(handle);
}

static mx_status_t my_process_get_info(mx_handle_t handle, mx_process_info_t* info)
{
    return _magenta_process_get_info(handle, info, sizeof(*info));
}

static bool wait_readable(mx_handle_t handle) {
    mx_signals_t satisfied_signals, satisfiable_signals;
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    mx_status_t status = my_wait(&handle, &signals, 1, NULL, MX_TIME_INFINITE,
                                 &satisfied_signals, &satisfiable_signals);
    if (status != NO_ERROR) {
        printf("process-test: my_wait returned %d\n", status);
        return false;
    } else if ((satisfied_signals & MX_SIGNAL_READABLE) == 0) {
        printf("process-test: my_wait peer closed\n");
        return false;
    }
    return true;
}

static bool wait_signalled(mx_handle_t handle) {
    mx_signals_t satisfied_signals, satisfiable_signals;
    mx_signals_t signals = MX_SIGNAL_SIGNALED;
    mx_status_t status = my_wait(&handle, &signals, 1, NULL, MX_TIME_INFINITE,
                                 &satisfied_signals, &satisfiable_signals);
    if (status != NO_ERROR) {
        printf("process-test: my_wait returned %d\n", status);
        return false;
    }
    return true;
}

int main(void) {
    mx_handle_t child_handle, pipe1, pipe2;

    mx_status_t status = my_create_message_pipe(&pipe1, &pipe2);
    if (status == NO_ERROR) {
        printf("process-test: created message pipe: %u %u\n", pipe1, pipe2);
    } else {
        printf("process-test: my_create_message_pipe failed %d\n", status);
        return status;
    }

    static const char child_name[] = "child-process";
    printf("process-test: starting process \"%s\"\n", child_name);
    child_handle = my_process_create(child_name, sizeof(child_name));
    printf("process-test: my_process_create returned %d\n", child_handle);
    if (child_handle < 0)
        return -1;

    uintptr_t entry;
    status = my_process_load(child_handle, "/boot/bin/child-process", &entry);
    printf("process-test: my_process_load returned %d\n", status);
    if (status != NO_ERROR) {
        my_close(pipe1);
        my_close(child_handle);
        return -1;
    }

    status = my_process_start(child_handle, pipe2, entry);
    printf("process-test: my_process_start returned %d\n", status);
    if (status != NO_ERROR) {
        my_close(pipe1);
        my_close(child_handle);
        return -1;
    }

    char buffer[64];
    uint32_t buffer_size = sizeof(buffer);
    snprintf(buffer, buffer_size, "Hi there!");
    status = my_write_message(pipe1, buffer, buffer_size, NULL, 0, 0);
    printf("process-test: my_write_message returned %d\n", status);

    if (!wait_readable(pipe1))
        return -1;

    buffer_size = sizeof(buffer);
    memset(buffer, 0, sizeof(buffer));
    status = my_read_message(pipe1, buffer, &buffer_size, NULL, NULL, 0);
    printf("process-test: my_read_message returned %d\n", status);
    printf("process-test: received \"%s\"\n", buffer);
    if (strcmp(buffer, "Hi there to you too!") != 0) {
        printf("process-test: unexpected message from child\n");
        return -1;
    }

    printf("process-test: done\n");

    my_close(pipe1);

    mx_process_info_t info;
    if (!wait_signalled(child_handle)) return -1;
    status = my_process_get_info(child_handle, &info);
    printf("process-test: my_process_get_info returned %d\n", status);
    if (status != NO_ERROR)
        return 1;
    printf("child process returned %d\n", info.return_code);
    if (info.return_code != 1234)
        return -1;

    my_close(child_handle);
    return 0;
}
