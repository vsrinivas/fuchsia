// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _ALL_SOURCE
#include <zircon/threads.h>
#include <zircon/syscalls.h>

#include <string>

#include "src/lib/fxl/strings/string_printf.h"

// Simple application that prints from several threads.

constexpr int kThreadCount = 5;
struct ThreadContext {
  int index = 0;
  std::string name;
};

int ThreadFunction(void* in) {
  ThreadContext* ctx = reinterpret_cast<ThreadContext*>(in);
  for (int i = 0; i < 50; i++) {
    printf("%s: message %d\n", ctx->name.c_str(), i);
    fflush(stdout);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500 * (ctx->index + 1))));
  }

  return 0;
}

int main() {
  thrd_t threads[kThreadCount];
  ThreadContext contexts[kThreadCount];

  for (int i = 0; i < kThreadCount; i++) {
    contexts[i].index = i;
    contexts[i].name = fxl::StringPrintf("thread-%d", i);
    thrd_create_with_name(threads + i, ThreadFunction, contexts + i,
                          contexts[i].name.c_str());
  }


  for (int i = 0; i < kThreadCount; i++) {
    int res = 0;
    thrd_join(threads[i], &res);
  }
}
