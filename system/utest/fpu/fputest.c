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
#include <unistd.h>

#include <magenta/syscalls.h>
#include <mxu/unittest.h>

#define THREAD_COUNT 8
#define ITER 1000000

#define __OPTIMIZE(x) __attribute__((optimize(x)))

#define countof(a) (sizeof(a) / sizeof((a)[0]))

/* expected double bit pattern for each thread */
static const uint64_t expected[THREAD_COUNT] = {
    0x4284755ed4188b3e,
    0x4284755ed6cb84c0,
    0x4284755ed97e7dd3,
    0x4284755edc317770,
    0x4284755edee471b9,
    0x4284755ee1976c19,
    0x4284755ee44a648b,
    0x4284755ee6fd5fa7,
};

/* optimize this function to cause it to try to use a lot of registers */
__OPTIMIZE("O3")
static int float_thread(void* arg) {
    double* val = arg;
    unsigned int i, j;
    double a[16];

    unittest_printf("float_thread arg %f, running %u iterations\n", *val, ITER);
    usleep(500000);

    /* do a bunch of work with floating point to test context switching */
    a[0] = *val;
    for (i = 1; i < countof(a); i++) {
        a[i] = a[i - 1] * 1.01;
    }

    for (i = 0; i < ITER; i++) {
        a[0] += i;
        for (j = 1; j < countof(a); j++) {
            a[j] += a[j - 1] * 0.00001;
        }
    }

    *val = a[countof(a) - 1];
    _magenta_thread_exit();
    return 0;
}

bool fpu_test(void) {
    BEGIN_TEST;

    unittest_printf("welcome to floating point test\n");

    /* test lazy fpu load on separate thread */
    mx_handle_t t[THREAD_COUNT];
    double val[countof(t)];
    char name[MX_MAX_NAME_LEN];

    unittest_printf("creating %zu floating point threads\n", countof(t));
    for (unsigned int i = 0; i < countof(t); i++) {
        val[i] = i;
        snprintf(name, sizeof(name), "fpu thread %u", i);
        t[i] = _magenta_thread_create(float_thread, &val[i], name, strlen(name));
    }

    for (unsigned int i = 0; i < countof(t); i++) {
        _magenta_handle_wait_one(t[i], MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
        _magenta_handle_close(t[i]);
        void* v = &val[i];
        uint64_t int64_val = *(uint64_t*)v;

        unittest_printf("float thread %u returns val %f 0x%llx, expected 0x%llx\n", i, val[i], int64_val, expected[i]);
        EXPECT_EQ(int64_val, expected[i], "Value does not match as expected");
    }

    unittest_printf("floating point test done\n");
    END_TEST;
}

BEGIN_TEST_CASE(fpu_tests)
RUN_TEST(fpu_test);
END_TEST_CASE(fpu_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests();
    return success ? 0 : -1;
}
