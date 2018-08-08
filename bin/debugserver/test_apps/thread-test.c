// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <zircon/compiler.h>
#define _ALL_SOURCE
#include <threads.h>
#include <unistd.h>

const int kNumberOfThreads = 4;

static void Fatal(const char* msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

static void ThreadCreate(thrd_t* t, thrd_start_t entry, void* arg,
                         const char* name) {
  int ret = thrd_create_with_name(t, entry, arg, name);
  if (ret != thrd_success) {
    // tu_fatal takes zx_status_t values.
    // The translation doesn't have to be perfect.
    switch (ret) {
      case thrd_nomem:
        Fatal("thread create failed, OOM");
      default:
        Fatal("thread create failed");
    }
    __UNREACHABLE;
  }
}

static int ThreadFunc(void* arg) {
  const char* name = arg;
  printf("Thread %s starting\n", name);
  sleep(1 + (random() % 4));
  printf("Thread %s exiting\n", name);
  return 0;
}

int main(void) {
  printf("thread-test\n");

  thrd_t threads[kNumberOfThreads];
  char* thread_names[kNumberOfThreads];

  for (int i = 0; i < kNumberOfThreads; ++i) {
    if (asprintf(&thread_names[i], "t%d", i) < 0)
      Fatal("thread name creation failed");
    ThreadCreate(&threads[i], ThreadFunc, thread_names[i], thread_names[i]);
  }

  for (int i = 0; i < kNumberOfThreads; ++i) {
    thrd_join(threads[i], NULL);
  }

  printf("thread-test exiting\n");
  return 0;
}
