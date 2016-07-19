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

#include <sched.h>
#include <stdio.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <runtime/thread.h>
#include <runtime/tls.h>

static uint64_t test_values[] = {
    0x0000000000000000ull, 0xffffffffffffffffull, 0x5555555555555555ull,
    0xaaaaaaaaaaaaaaaaull, 0x0123456789abcdefull, 0xfedcba9876543210ull,
    0xababababababababull, 0x912f277f61b583a5ull, 0x3b7c08b96d727cedull,
};

static mxr_tls_t keys[MXR_TLS_SLOT_MAX];
static volatile size_t num_keys;

static int test_entry_point(void* arg) {
    uintptr_t id = (uintptr_t)arg;
    uintptr_t values[MXR_TLS_SLOT_MAX];

    // Test that slots are zeroed out on creation.
    for (size_t idx = 0; idx < num_keys; idx++) {
        ASSERT_EQ(mxr_tls_get(keys[idx]), NULL, "Inital slots not zeroed");
    }

    // Test setting valid values.
    for (size_t value_idx = 0; value_idx < sizeof(test_values) / sizeof(*test_values); ++value_idx) {
        uintptr_t value = test_values[value_idx] ^ id;
        for (uintptr_t iteration = 0; iteration < 0x10ull; ++iteration) {
            for (size_t idx = 0; idx < num_keys; ++idx) {
                values[idx] = value;
                values[idx] ^= (iteration << 12);
                values[idx] ^= (idx << 16);
                mxr_tls_set(keys[idx], (void*)values[idx]);
            }
            sched_yield();
            for (size_t idx = 0; idx < num_keys; ++idx) {
                uintptr_t new_value = (uintptr_t)mxr_tls_get(keys[idx]);
                ASSERT_EQ(new_value, values[idx], "tls_get returned wrong value");
            }
        }
    }

    return 0;
}

bool mxr_tls_test(void) {
    BEGIN_TEST;

    for (;;) {
        mxr_tls_t key = mxr_tls_allocate();
        if (key == MXR_TLS_SLOT_INVALID)
            break;
        // We shouldn't allocate too many slots.
        ASSERT_LT(num_keys, MXR_TLS_SLOT_MAX, "Too many slots allocated");
        keys[num_keys++] = key;
    }

    ASSERT_GT(num_keys, 0u, "num_keys should be more than 0");

#define num_threads 64

    mxr_thread_t* threads[num_threads] = {0};

    for (uintptr_t idx = 0; idx < num_threads; ++idx) {
        mx_status_t status = mxr_thread_create(test_entry_point, (void*)idx, "mxr tls test", threads + idx);
        ASSERT_EQ(status, NO_ERROR, "Error while thread creation");
    }

    for (uintptr_t idx = 0; idx < num_threads; ++idx) {
        mx_status_t status = mxr_thread_join(threads[idx], NULL);
        if (status != NO_ERROR)
        ASSERT_EQ(status, NO_ERROR, "Error while thread join");
    }

    test_entry_point((void*)(uintptr_t)num_threads);
    END_TEST;
}

BEGIN_TEST_CASE(mxr_tls_tests)
RUN_TEST(mxr_tls_test)
END_TEST_CASE(mxr_tls_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
