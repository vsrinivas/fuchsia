// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/backtrace-request/backtrace-request.h>
#include <lib/zx/event.h>
#include <stdio.h>

#include <thread>

// Program that sets up a number on thread at the cusp of some recursive calls and then calls a
// backtrace request in order to get all of them printed.

namespace {

// |from_event| is used from the creator to make this thread wait before finishing.
// |to_event| is an event this thread triggers when it is at the top frame of the recursive calls.
void SomeDeepStack(const zx::event* from_event, const zx::event* to_event, int id, int count) {
  if (count > 0) {
    SomeDeepStack(from_event, to_event, id, count - 1);
    return;
  }

  to_event->signal(0, ZX_USER_SIGNAL_0);
  from_event->wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);

  // Create the backtrace request.
  printf("Thread %d done.\n", id);
}
constexpr int kThreadCount = 4;

zx::event CreateEvent() {
  zx::event event;
  zx::event::create(0, &event);
  return event;
}

}  // namespace

int main() {
  zx::event wait_event = CreateEvent();
  zx::event events[kThreadCount];
  for (auto& event : events) {
    event = CreateEvent();
  }

  // Create all the threads.
  std::thread threads[kThreadCount];
  for (int i = 0; i < kThreadCount; i++) {
    threads[i] = std::thread(SomeDeepStack, &wait_event, &events[i], i, i);
  }

  // Wait for all of them to reach the end of the stack.
  for (int i = 0; i < kThreadCount; i++) {
    events[i].wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
    printf("Thread %d is ready.\n", i);
  }

  // Create the backtrace request.
  printf("Doing backtrace request.\n");
  backtrace_request();

  // Tell all the threads to finish and join them.
  wait_event.signal(0, ZX_USER_SIGNAL_0);
  for (auto& thread : threads) {
    thread.join();
  }

  printf("Done doing backtrace request.\n");
}
