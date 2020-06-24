// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

static void* do_test(void* arg) {
  int thread_no = *(int*)arg;
  printf("do_test for thread: %d\n", thread_no);
  errno = -thread_no;
  zx_nanosleep(zx_deadline_after(ZX_MSEC(300)));
  printf("comparing result for: %d\n", thread_no);
  EXPECT_EQ(errno, -thread_no, "Incorrect errno for this thread");
  return NULL;
}

TEST(ErrnoTests, errno_test) {
  int main_thread = 1, thread_1 = 2, thread_2 = 3;

  pthread_t thread2, thread3;

  printf("creating thread: %d\n", thread_1);
  pthread_create(&thread2, NULL, do_test, &thread_1);

  printf("creating thread: %d\n", thread_2);
  pthread_create(&thread3, NULL, do_test, &thread_2);

  do_test(&main_thread);

  printf("joining thread: %d\n", thread_1);
  pthread_join(thread2, NULL);

  printf("joining thread: %d\n", thread_2);
  pthread_join(thread3, NULL);
}
