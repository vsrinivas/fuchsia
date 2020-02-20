// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdio.h>
#include <zircon/types.h>

#include <kernel/thread.h>

#include "tests.h"

static int fibo_thread(void* argv) {
  long fibo = (intptr_t)argv;

  Thread* t[2];

  if (fibo == 0)
    return 0;
  if (fibo == 1)
    return 1;

  char name[32];
  snprintf(name, sizeof(name), "fibo %ld", fibo - 1);
  t[0] = Thread::Create(name, &fibo_thread, (void*)(fibo - 1), DEFAULT_PRIORITY);
  if (!t[0]) {
    printf("error creating thread for fibo %ld\n", fibo - 1);
    return 0;
  }
  snprintf(name, sizeof(name), "fibo %ld", fibo - 2);
  t[1] = Thread::Create(name, &fibo_thread, (void*)(fibo - 2), DEFAULT_PRIORITY);
  if (!t[1]) {
    printf("error creating thread for fibo %ld\n", fibo - 2);
    t[0]->Resume();
    t[0]->Join(NULL, ZX_TIME_INFINITE);
    return 0;
  }

  t[0]->Resume();
  t[1]->Resume();

  int retcode0, retcode1;

  t[0]->Join(&retcode0, ZX_TIME_INFINITE);
  t[1]->Join(&retcode1, ZX_TIME_INFINITE);

  return retcode0 + retcode1;
}

int fibo(int argc, const cmd_args* argv, uint32_t) {
  if (argc < 2) {
    printf("not enough args\n");
    return -1;
  }

  zx_time_t tim = current_time();

  Thread* t = Thread::Create("fibo", &fibo_thread, (void*)(uintptr_t)argv[1].u, DEFAULT_PRIORITY);
  t->Resume();

  int retcode;
  t->Join(&retcode, ZX_TIME_INFINITE);

  zx_duration_t msec_duration = (current_time() - tim) / ZX_MSEC(1);

  printf("fibo %d\n", retcode);
  printf("took %" PRIi64 " msecs to calculate\n", msec_duration);

  return ZX_OK;
}
