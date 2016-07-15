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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

static uint32_t lcg_rand(uint32_t seed) {
    return (seed = (seed * 1664525u) + 1013904223u);
}

// Fill a region of memory with a pattern. The seed is returned
// so that the fill can be done in chunks. When done so, you need to
// store the seed if you want to test the memory in chunks.
static uint32_t fill_region(void* _ptr, size_t len, uint32_t seed) {
    uint32_t* ptr = (uint32_t*)_ptr;
    uint32_t val = seed;

    for (size_t i = 0; i < len / 4; i++) {
        ptr[i] = val;
        val = lcg_rand(val);
    }
    return val;
}

// Test a region of memory against a a fill with fill_region().
static bool test_region(void* _ptr, size_t len, uint32_t seed) {
    uint32_t* ptr = (uint32_t*)_ptr;
    uint32_t val = seed;

    for (size_t i = 0; i < len / 4; i++) {
        if (ptr[i] != val) {
            unittest_printf("wrong value at %p (%zu): 0x%x vs 0x%x\n", &ptr[i], i, ptr[i], val);
            return false;
        }
        val = lcg_rand(val);
    }
    return true;
}

#define KB_(x) (x*1024)

static bool create_destroy_test(void) {
    BEGIN_TEST;
    mx_status_t status;
    mx_handle_t producer;
    mx_handle_t consumer;

    producer = mx_data_pipe_create(0u, 1u, KB_(1), &consumer);
    ASSERT_GE(producer, 0, "could not create producer data pipe");
    ASSERT_GE(consumer, 0, "could not create consumer data pipe");

    status = mx_data_pipe_end_write(producer, 0u);
    ASSERT_EQ(status, ERR_BAD_STATE, "wrong pipe state");
    status = mx_data_pipe_end_read(consumer, 0u);
    ASSERT_EQ(status, ERR_BAD_STATE, "wrong pipe state");

    uintptr_t buffer = 0;
    mx_ssize_t avail;

    avail = mx_data_pipe_begin_write(consumer, 0u, 100u, &buffer);
    ASSERT_EQ(avail, ERR_BAD_HANDLE, "expected error");
    avail = mx_data_pipe_begin_read(producer, 0u, 100u, &buffer);
    ASSERT_EQ(avail, ERR_BAD_HANDLE, "expected error");

    status = mx_handle_close(producer);
    ASSERT_GE(status, NO_ERROR, "failed to close data pipe");
    status = mx_handle_close(consumer);
    ASSERT_GE(status, NO_ERROR, "failed to close data pipe");
    END_TEST;
}

static bool loop_write_full(void) {
    BEGIN_TEST;
    mx_handle_t producer;
    mx_handle_t consumer;
    mx_status_t status;

    producer = mx_data_pipe_create(0u, 1u, KB_(32), &consumer);
    ASSERT_GE(producer, 0, "could not create producer data pipe");
    ASSERT_GE(consumer, 0, "could not create consumer data pipe");

    for (int ix = 0; ; ++ix) {
        uintptr_t buffer = 0;
        mx_ssize_t avail = mx_data_pipe_begin_write(producer, 0u, KB_(4), &buffer);
        if (avail < 0) {
            ASSERT_EQ(avail, ERR_NOT_READY, "wrong error");
            ASSERT_EQ(ix, 8, "wrong capacity");
            break;
        }
        memset((void*)buffer, ix, KB_(4));
        status = mx_data_pipe_end_write(producer, KB_(4));
        ASSERT_EQ(status, NO_ERROR, "failed to end write");
    }

    status = mx_handle_close(consumer);
    ASSERT_GE(status, NO_ERROR, "failed to close data pipe");
    status = mx_handle_close(producer);
    ASSERT_GE(status, NO_ERROR, "failed to close data pipe");
    END_TEST;
}

static bool begin_write_read(void) {
    // Pipe of 32KB. Single write of 12000 bytes and 3 reads of 3000 bytes each.
    BEGIN_TEST;
    mx_handle_t producer;
    mx_handle_t consumer;
    mx_status_t status;

    producer = mx_data_pipe_create(0u, 1u, KB_(32), &consumer);
    ASSERT_GE(producer, 0, "could not create producer data pipe");
    ASSERT_GE(consumer, 0, "could not create consumer data pipe");

    uintptr_t buffer = 0;
    mx_ssize_t avail = mx_data_pipe_begin_write(producer, 0u, 4 * 3000u, &buffer);
    ASSERT_EQ(avail, 4 * 3000, "begin_write failed");

    uint32_t seed[5] = {7u, 0u, 0u, 0u, 0u};
    for (int ix = 0; ix != 4; ++ix) {
        seed[ix + 1] = fill_region((void*)buffer, 3000u, seed[ix]);
        buffer += 3000u;
    }

    status = mx_data_pipe_end_write(producer, 12000u);
    ASSERT_EQ(status, NO_ERROR, "failed to end write");

    status = mx_handle_close(producer);
    ASSERT_GE(status, NO_ERROR, "failed to close data pipe");

#if 0
    // At this point the vmo holds the data and its not mapped into any address space
    // uncommenting this block should make the test crash.
    cbw[16] = 0;
#endif

    for (int ix= 0; ix != 4; ++ix) {
        buffer = 0;
        avail = mx_data_pipe_begin_read(consumer, 0u, 3000u, &buffer);
        ASSERT_EQ(avail, 3000, "begin_write failed");

        bool equal = test_region((void*)buffer, 3000u, seed[ix]);
        ASSERT_EQ(equal, true, "invalid data");

        status = mx_data_pipe_end_read(consumer, 3000u);
        ASSERT_EQ(status, NO_ERROR, "failed to end read");
    }

    status = mx_handle_close(consumer);
    ASSERT_GE(status, NO_ERROR, "failed to close data pipe");
    END_TEST;
}

static bool loop_write_read(void) {
    BEGIN_TEST;
    mx_handle_t producer;
    mx_handle_t consumer;
    mx_status_t status;

    producer = mx_data_pipe_create(0u, 1u, KB_(36), &consumer);
    ASSERT_GE(producer, 0, "could not create producer data pipe");
    ASSERT_GE(consumer, 0, "could not create consumer data pipe");

    // The writter goes faster, after 10 rounds the write cursor catches up from behind.
    for (int ix = 0; ; ++ix) {
        uintptr_t buffer = 0;
        mx_ssize_t avail = mx_data_pipe_begin_write(producer, 0u, KB_(12), &buffer);
        if (avail != KB_(12)) {
            ASSERT_EQ(ix, 9, "bad cursor management");
            ASSERT_EQ(avail, KB_(9), "bad capacity");
            break;
        }

        memset((void*)buffer, ix, KB_(12));
        status = mx_data_pipe_end_write(producer, KB_(12));
        ASSERT_EQ(status, NO_ERROR, "failed to end write");

        avail = mx_data_pipe_begin_read(consumer, 0u, KB_(9), &buffer);
        ASSERT_EQ(avail, KB_(9), "begin_write failed");
        status = mx_data_pipe_end_read(consumer, KB_(9));
        ASSERT_EQ(status, NO_ERROR, "failed to end read");
    }

    status = mx_handle_close(consumer);
    ASSERT_GE(status, NO_ERROR, "failed to close data pipe");
    status = mx_handle_close(producer);
    ASSERT_GE(status, NO_ERROR, "failed to close data pipe");
    END_TEST;
}


BEGIN_TEST_CASE(data_pipe_tests)
RUN_TEST(create_destroy_test)
RUN_TEST(loop_write_full)
RUN_TEST(begin_write_read)
RUN_TEST(loop_write_read)
END_TEST_CASE(data_pipe_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
