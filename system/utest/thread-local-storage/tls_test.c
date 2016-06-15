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
#include <magenta/syscalls.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static int key_create(pthread_key_t* tsd_key) {
    int r = pthread_key_create(tsd_key, NULL);
    assert(r == 0);
    return r;
}

static int set_key_value(pthread_key_t key, void* value) {
    int r = pthread_setspecific(key, value);
    assert(r == 0);
    return r;
}

static pthread_key_t tsd_key1, tsd_key2;

static void test_tls(int thread_no) {
    int value1 = thread_no;
    int value2 = thread_no + 10;
    set_key_value(tsd_key1, &value1);
    set_key_value(tsd_key2, &value2);
    _magenta_nanosleep(100 * 1000 * 1000);
    int* v = pthread_getspecific(tsd_key1);
    assert(*v == value1);
    v = pthread_getspecific(tsd_key2);
    assert(*v == value2);
    printf("tls_test completed for thread: %d\n", thread_no);
}

static void* do_work(void* arg) {
    printf("do_work for thread: %d\n", *(int*)arg);
    test_tls(*(int*)arg);
    return NULL;
}

int main(void) {
#if defined ARCH_X86_64 || defined ARCH_ARM64
    key_create(&tsd_key1);
    key_create(&tsd_key2);

    // Run this 20 times for sanity check
    for (int i = 1; i <= 20; i++) {
        int main_thread = 1, thread_1 = i * 2, thread_2 = i * 2 + 1;

        pthread_t thread2, thread3;

        printf("creating thread: %d\n", thread_1);
        pthread_create(&thread2, NULL, do_work, &thread_1);

        printf("creating thread: %d\n", thread_2);
        pthread_create(&thread3, NULL, do_work, &thread_2);

        test_tls(main_thread);

        printf("joining thread: %d\n", thread_1);
        pthread_join(thread2, NULL);

        printf("joining thread: %d\n", thread_2);
        pthread_join(thread3, NULL);
    }
#endif
    return 0;
}
