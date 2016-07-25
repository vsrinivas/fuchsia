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

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/syscalls.h>
#include <unittest/test-utils.h>
#include <unittest/unittest.h>

bool process_test(void) {
    BEGIN_TEST;
    mx_handle_t child_handle, pipe1, pipe2;

    tu_message_pipe_create(&pipe1, &pipe2);
    unittest_printf("process-test: created message pipe: %u %u\n", pipe1, pipe2);

    static const char child_name[] = "child-process";
    unittest_printf("process-test: starting process \"%s\"\n", child_name);

    launchpad_t* lp;
    mx_status_t status = launchpad_create(child_name, &lp);

    unittest_printf("process-test: launchpad_create returned %d\n", status);
    ASSERT_EQ(status, 0, "launchpad_create failed");

    status = launchpad_elf_load_basic(
        lp, launchpad_vmo_from_file("/boot/bin/child-process"));
    unittest_printf("process-test: launchpad_elf_load_basic returned %d\n",
                    status);
    uintptr_t entry;
    if (status == NO_ERROR)
        status = launchpad_get_entry_address(lp, &entry);
    if (status != NO_ERROR) {
        tu_handle_close(pipe1);
        ASSERT_EQ(status, 0, "error loading child process");
    }

    child_handle = launchpad_get_process_handle(lp);
    ASSERT_GE(child_handle, 0, "launchpad_get_process_handle failed");

    status = mx_process_start(child_handle, pipe2, entry);
    unittest_printf("process-test: mx_process_start returned %d\n", status);
    if (status != NO_ERROR) {
        tu_handle_close(pipe1);
        launchpad_destroy(lp);
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

    launchpad_destroy(lp);

    END_TEST;
}

BEGIN_TEST_CASE(process_tests)
RUN_TEST(process_test)
END_TEST_CASE(process_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
