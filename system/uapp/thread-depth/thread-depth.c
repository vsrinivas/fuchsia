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
#include <time.h>

#include <runtime/thread.h>
#include <magenta/syscalls.h>
#include <system/atomic.h>

static uint64_t count = 0;

static int thread_func(void* arg) {
    uint64_t val = atomic_add_uint64(&count, 1);
    val++;
    if (val % 1000 == 0) {
        printf("Created %lld threads, time %lld us\n", val, mx_current_time() / 1000000);
    }

    mxr_thread_t *thread = NULL;
    mx_status_t status = mxr_thread_create(thread_func, NULL, "depth", &thread);
    if (status == NO_ERROR) {
        status = mxr_thread_join(thread, NULL);
        if (status != NO_ERROR) {
            printf("Unexpected thread join return: %d\n", status);
            return 1;
        }
        val = atomic_add_uint64(&count, -1);
        val--;
        if (val % 1000 == 0)
            printf("Joined %lld threads, time %lld us\n", val, mx_current_time() / 1000000);
    }

    return 0;
}

int main(int argc, char** argv) {
    printf("Running thread depth test...\n");

    mxr_thread_t *thread = NULL;
    mx_status_t status = mxr_thread_create(thread_func, NULL, "depth", &thread);
    status = mxr_thread_join(thread, NULL);
    if (status != NO_ERROR) {
        printf("Unexpected thread join return: %d\n", status);
        return 1;
    }

    printf("Created %lld threads\n", count);
    return 0;
}
