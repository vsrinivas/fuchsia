// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_SYSTEM_RUN_IN_THREAD_H_
#define APPS_LEDGER_SRC_GLUE_SYSTEM_RUN_IN_THREAD_H_

#include <pthread.h>

namespace ledger {

namespace internal {

constexpr size_t kDefaultStackSize = 1 * 1024 * 1024;

template <typename A>
struct RunInThreadData {
  std::function<A()> runnable;
  A* result;

  static void* Run(void* context) {
    RunInThreadData<A>* data = static_cast<RunInThreadData<A>*>(context);
    *(data->result) = data->runnable();
    return nullptr;
  }
};

}  // namespace internal

// Runs |runnable| in a new thread with a stack size of |stack_size|.  Returns 0
// in case of success, and |result| will contain the return value of |runnable|.
template <typename A>
int RunInThread(std::function<A()> runnable,
                A* result,
                size_t stack_size = internal::kDefaultStackSize) {
  internal::RunInThreadData<A> data{std::move(runnable), result};

  pthread_attr_t attr;
  pthread_t thread;

  auto thread_result = pthread_attr_init(&attr);
  if (thread_result != 0) {
    return thread_result;
  }

  thread_result =
      pthread_attr_setstacksize(
          &attr, std::max<size_t>(PTHREAD_STACK_MIN, stack_size)) != 0;
  if (thread_result != 0) {
    return thread_result;
  }

  thread_result = pthread_create(&thread, &attr,
                                 &internal::RunInThreadData<A>::Run, &data);
  if (thread_result != 0) {
    return thread_result;
  }

  pthread_attr_destroy(&attr);

  thread_result = pthread_join(thread, nullptr);

  if (thread_result != 0) {
    return thread_result;
  };

  return 0;
}

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_GLUE_SYSTEM_RUN_IN_THREAD_H_
