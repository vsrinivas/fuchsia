// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/sync/cpp/completion.h>
#include <stdio.h>
#include <zircon/compiler.h>

#include <zxtest/zxtest.h>

__EXPORT int main(int argc, char** argv) {
  setlinebuf(stdout);
  const void* driver = reinterpret_cast<void*>(0x12345678);
  fdf_internal_push_driver(driver);
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, [](fdf_dispatcher_t*) {});
  fdf_internal_pop_driver();
  if (dispatcher.is_error()) {
    return dispatcher.status_value();
  }

  zx_status_t status;
  libsync::Completion completion;
  async::PostTask(dispatcher->async_dispatcher(), [&]() {
    status = RUN_ALL_TESTS(argc, argv);
    completion.Signal();
  });
  // Dispatcher will be destroyed by |fdf_internal_destroy_all_dispatchers() below.
  dispatcher->release();
  completion.Wait();

  class Observer : public fdf_internal_driver_shutdown_observer_t {
   public:
    Observer()
        : fdf_internal_driver_shutdown_observer_t(
              fdf_internal_driver_shutdown_observer_t{.handler = &Observer::Handler}) {}

    static void Handler(const void* driver, fdf_internal_driver_shutdown_observer_t* observer) {
      static_cast<Observer*>(observer)->completion_.Signal();
    }

    void Wait() { completion_.Wait(); }

   private:
    libsync::Completion completion_;
  };
  Observer observer;
  fdf_internal_shutdown_dispatchers_async(driver, &observer);
  observer.Wait();

  fdf_internal_destroy_all_dispatchers();
  return status;
}
