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

#include <magenta/syscalls.h>
#include <mxio/util.h>
#include <unittest/test-utils.h>
#include <unittest/unittest.h>

static mx_handle_t my_process_create(const char* name, uint32_t name_len) {
    return mx_process_create(name, name_len);
}

static mx_status_t my_process_load(mx_handle_t process, const char* elf_file, uintptr_t* out_entry) {
    int fd = open(elf_file, O_RDONLY);
    if (fd < 0)
        return ERR_IO;

    uintptr_t entry = 0;
    mx_status_t status = mxio_load_elf_fd(process, &entry, fd);
    close(fd);
    if (status < 0)
        return status;

    *out_entry = entry;
    return NO_ERROR;
}

static mx_status_t my_process_start(mx_handle_t process, mx_handle_t handle, uintptr_t entry) {
    return mx_process_start(process, handle, entry);
}

bool process_test(void) {
    BEGIN_TEST;
    mx_handle_t child_handle, pipe1, pipe2;

    tu_message_pipe_create(&pipe1, &pipe2);
    unittest_printf("process-test: created message pipe: %u %u\n", pipe1, pipe2);

    static const char child_name[] = "child-process";
    unittest_printf("process-test: starting process \"%s\"\n", child_name);
    child_handle = my_process_create(child_name, sizeof(child_name));
    unittest_printf("process-test: my_process_create returned %d\n", child_handle);
    ASSERT_GE(child_handle, 0, "child handle invalid");

    uintptr_t entry;
    mx_status_t status = my_process_load(child_handle, "/boot/bin/child-process", &entry);
    unittest_printf("process-test: my_process_load returned %d\n", status);
    if (status != NO_ERROR) {
        tu_handle_close(pipe1);
        tu_handle_close(child_handle);
        ASSERT_TRUE(false, "error loading child process");
    }

    status = my_process_start(child_handle, pipe2, entry);
    unittest_printf("process-test: my_process_start returned %d\n", status);
    if (status != NO_ERROR) {
        tu_handle_close(pipe1);
        tu_handle_close(child_handle);
        ASSERT_TRUE(false, "error starting child process");
    }

    char buffer[64];
    uint32_t buffer_size = sizeof(buffer);
    snprintf(buffer, buffer_size, "Hi there!");
    status = mx_message_write(pipe1, buffer, buffer_size, NULL, 0, 0);
    unittest_printf("process-test: my_write_message returned %d\n", status);

    ASSERT_TRUE(tu_wait_readable(pipe1), "pipe1 closed");

    buffer_size = sizeof(buffer);
    memset(buffer, 0, sizeof(buffer));
    status = mx_message_read(pipe1, buffer, &buffer_size, NULL, NULL, 0);
    unittest_printf("process-test: my_read_message returned %d\n", status);
    unittest_printf("process-test: received \"%s\"\n", buffer);
    ASSERT_BYTES_EQ((uint8_t*)buffer, (uint8_t*)"Hi there to you too!", buffer_size,
                    "process-test: unexpected message from child");

    unittest_printf("process-test: done\n");

    tu_handle_close(pipe1);

    tu_wait_signalled(child_handle);

    int return_code = tu_process_get_return_code(child_handle);
    ASSERT_EQ(return_code, 1234, "Invalid child process return code");

    tu_handle_close(child_handle);

    END_TEST;
}

BEGIN_TEST_CASE(process_tests)
RUN_TEST(process_test)
END_TEST_CASE(process_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
