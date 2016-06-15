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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>
#include <runtime/thread.h>

#include <runtime/tls.h>

volatile int threads_done[7];

static int thread_entry(void* arg) {
    int thread_number = (int)(intptr_t)arg;
    errno = thread_number;
    printf("thread %d sleeping for .1 seconds\n", thread_number);
    _magenta_nanosleep(100 * 1000 * 1000);
    if (errno != thread_number) {
        printf("errno changed by someone!\n");
        exit(-1);
    }
    threads_done[thread_number] = 1;
    return thread_number;
}

int main(void) {
    mxr_thread_t* thread;
    mx_status_t status;
    int return_value;

    printf("Welcome to thread test!\n");

    for (int i = 0; i != 4; ++i) {
        status = mxr_thread_create(thread_entry, (void*)(intptr_t)i, "mxr thread test", &thread);
        if (status != NO_ERROR)
            exit(status);
        status = mxr_thread_join(thread, &return_value);
        if (status != NO_ERROR)
            exit(status);
        if (return_value != i)
            exit(-1);
    }

    printf("Attempting to create thread with a super long name. This should fail\n");
    status = mxr_thread_create(thread_entry, NULL,
                               "01234567890123456789012345678901234567890123456789012345678901234567890123456789", &thread);
    if (status == NO_ERROR)
        exit(-2);
    printf("Attempting to create thread with a null name. This should succeed\n");
    status = mxr_thread_create(thread_entry, (void*)(intptr_t)4, NULL, &thread);
    if (status != NO_ERROR)
        exit(status);
    status = mxr_thread_join(thread, &return_value);
    if (status != NO_ERROR)
        exit(status);
    if (return_value != 4)
        exit(-3);

    status = mxr_thread_create(thread_entry, (void*)(intptr_t)5, NULL, &thread);
    if (status != NO_ERROR)
        exit(status);
    status = mxr_thread_detach(thread);
    if (status != NO_ERROR)
        exit(status);
    while (!threads_done[5])
        _magenta_nanosleep(100 * 1000 * 1000);

    thread_entry((void*)(intptr_t)6);
    if (!threads_done[6])
        exit(-4);

    printf("thread test done\n");

    return 0;
}
