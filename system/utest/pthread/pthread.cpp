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

#include <pthread.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int process_waked = 0;
int thread_with_lock = 0;

static void log(const char* str) {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    unittest_printf("[%08lu.%08lu]: %s", time.tv_sec, time.tv_nsec / 1000, str);
}

static void* mutex_thread_1(void* arg) {
    log("thread 1 grabbing mutex\n");
    pthread_mutex_lock(&mutex);
    log("thread 1 got mutex\n");
    thread_with_lock = 1;
    mx_nanosleep(300 * 1000 * 1000);

    // Make sure no other thread woke up
    EXPECT_EQ(thread_with_lock, 1, "Only thread 1 should have woken up");
    log("thread 1 releasing mutex\n");
    pthread_mutex_unlock(&mutex);
    log("thread 1 done\n");
    return NULL;
}

static void* mutex_thread_2(void* arg) {
    mx_nanosleep(100 * 1000 * 1000);
    log("thread 2 grabbing mutex\n");
    pthread_mutex_lock(&mutex);
    log("thread 2 got mutex\n");
    thread_with_lock = 2;

    mx_nanosleep(300 * 1000 * 1000);

    // Make sure no other thread woke up
    EXPECT_EQ(thread_with_lock, 2, "Only thread 2 should have woken up");

    log("thread 2 releasing mutex\n");
    pthread_mutex_unlock(&mutex);
    log("thread 2 done\n");
    return NULL;
}

static void* mutex_thread_3(void* arg) {
    mx_nanosleep(100 * 1000 * 1000);
    log("thread 3 grabbing mutex\n");
    pthread_mutex_lock(&mutex);
    log("thread 3 got mutex\n");
    thread_with_lock = 3;

    mx_nanosleep(300 * 1000 * 1000);

    // Make sure no other thread woke up
    EXPECT_EQ(thread_with_lock, 3, "Only thread 3 should have woken up");

    log("thread 3 releasing mutex\n");
    pthread_mutex_unlock(&mutex);
    log("thread 3 done\n");
    return NULL;
}

static void* cond_thread1(void* arg) {
    pthread_mutex_lock(&mutex);
    log("thread 1 waiting on condition\n");
    pthread_cond_wait(&cond, &mutex);
    log("thread 2 waiting again\n");
    pthread_cond_wait(&cond, &mutex);
    process_waked++;
    pthread_mutex_unlock(&mutex);
    log("thread 1 done\n");
    return NULL;
}

static void* cond_thread2(void* arg) {
    pthread_mutex_lock(&mutex);
    log("thread 2 waiting on condition\n");
    pthread_cond_wait(&cond, &mutex);
    log("thread 2 waiting again\n");
    pthread_cond_wait(&cond, &mutex);
    process_waked++;
    pthread_mutex_unlock(&mutex);
    log("thread 2 done\n");
    return NULL;
}

static void* cond_thread3(void* arg) {
    pthread_mutex_lock(&mutex);
    log("thread 3 waiting on condition\n");
    pthread_cond_wait(&cond, &mutex);
    log("thread 3 waiting again\n");
    pthread_cond_wait(&cond, &mutex);
    process_waked++;
    pthread_mutex_unlock(&mutex);
    log("thread 3 done\n");
    return NULL;
}

bool pthread_test(void) {

    BEGIN_TEST;
    pthread_t thread1, thread2, thread3;

    log("testing uncontested case\n");
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    log("mutex locked and unlocked\n");

    log("starting cond threads\n");
    pthread_create(&thread1, NULL, cond_thread1, NULL);
    pthread_create(&thread2, NULL, cond_thread2, NULL);
    pthread_create(&thread3, NULL, cond_thread3, NULL);

    mx_nanosleep(300 * 1000 * 1000);

    log("calling pthread_cond_broadcast\n");
    pthread_cond_broadcast(&cond);

    mx_nanosleep(100 * 1000 * 1000);
    log("calling pthread_cond_signal\n");
    pthread_cond_signal(&cond);
    mx_nanosleep(300 * 1000 * 1000);
    EXPECT_EQ(process_waked, 1, "Only 1 process should have woken up");

    log("calling pthread_cond_signal\n");
    pthread_cond_signal(&cond);
    mx_nanosleep(100 * 1000 * 1000);
    EXPECT_EQ(process_waked, 2, "Only 2 processes should have woken up");

    log("calling pthread_cond_signal\n");
    pthread_cond_signal(&cond);
    mx_nanosleep(100 * 1000 * 1000);
    EXPECT_EQ(process_waked, 3, "Only 3 processes should have woken up");

    log("joining cond threads\n");
    pthread_join(thread1, NULL);
    log("cond_thread 1 joined\n");
    pthread_join(thread2, NULL);
    log("cond_thread 2 joined\n");
    pthread_join(thread3, NULL);
    log("cond_thread 3 joined\n");

    pthread_mutex_lock(&mutex);
    log("waiting on condition with 2 second timeout\n");
    struct timespec delay;
    clock_gettime(CLOCK_REALTIME, &delay);
    delay.tv_sec += 2;
    int result = pthread_cond_timedwait(&cond, &mutex, &delay);
    pthread_mutex_unlock(&mutex);
    log("pthread_cond_timedwait returned\n");
    printf("pthread_cond_timedwait result: %d\n", result);

    EXPECT_EQ(result, ETIMEDOUT, "Lock should have timeout");

    log("creating mutex threads\n");
    pthread_create(&thread1, NULL, mutex_thread_1, NULL);
    pthread_create(&thread2, NULL, mutex_thread_2, NULL);
    pthread_create(&thread3, NULL, mutex_thread_3, NULL);

    log("joining mutex threads\n");
    pthread_join(thread1, NULL);
    log("thread 1 joined\n");
    pthread_join(thread2, NULL);
    log("thread 2 joined\n");
    pthread_join(thread3, NULL);
    log("thread 3 joined\n");

    END_TEST;
}


BEGIN_TEST_CASE(pthread_tests)
RUN_TEST(pthread_test)
END_TEST_CASE(pthread_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
