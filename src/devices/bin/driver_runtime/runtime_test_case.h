// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_RUNTIME_TEST_CASE_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_RUNTIME_TEST_CASE_H_

#include <lib/fdf/arena.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/types.h>
#include <lib/sync/completion.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/event.h>

#include <zxtest/zxtest.h>

class RuntimeTestCase : public zxtest::Test {
 public:
  // Registers a wait_async request on |ch| and signals |completion| once it
  // is ready for reading.
  static void SignalOnChannelReadable(fdf_handle_t ch, fdf_dispatcher_t* dispatcher,
                                      sync_completion_t* completion);

  // Registers a wait_async request on |ch| and blocks until it is ready for reading.
  static void WaitUntilReadReady(fdf_handle_t ch, fdf_dispatcher_t* dispatcher);

  // Reads a message from |ch| and asserts that it matches the wanted parameters.
  // If |out_arena| is provided, it will be populated with the transferred arena.
  static void AssertRead(fdf_handle_t ch, void* want_data, size_t want_num_bytes,
                         zx_handle_t* want_handles, uint32_t want_num_handles,
                         fdf_arena_t** out_arena = nullptr);

  // Returns a fake driver pointer that can be used with driver_context APIs.
  // Do not try to access the internals of the pointer.
  const void* CreateFakeDriver() {
    // We don't actually need a real pointer.
    int driver = next_driver_;
    next_driver_++;
    return reinterpret_cast<const void*>(driver);
  }

  // Example usage:
  //   DispatcherShutdownObserver observer;
  //   driver_runtime::Dispatcher* dispatcher;
  //   fdf_status_t status =
  //       driver_runtime::Dispatcher::Create(..., observer.fdf_observer(), &dispatcher);
  //   ...
  //   dispatcher->Destroy();
  //   ASSERT_OK(observer.WaitUntilShutdown());
  class DispatcherShutdownObserver {
   public:
    // |require_callback| specifies whether the destructor will check that the callback was called.
    // This can be set to false for tests that expect construction of the dispatcher to fail,
    // but want to pass in a valid observer.
    explicit DispatcherShutdownObserver(bool require_callback = true) : require_callback_(require_callback) {
      observer_.fdf_observer.handler = DispatcherDestructedHandler;
    }

    ~DispatcherShutdownObserver() {
      if (require_callback_) {
        ASSERT_TRUE(observer_.signal.signaled());
      }
    }

    DispatcherShutdownObserver(const DispatcherShutdownObserver&) = delete;
    DispatcherShutdownObserver& operator=(const DispatcherShutdownObserver&) = delete;
    DispatcherShutdownObserver& operator=(DispatcherShutdownObserver&&) = delete;
    DispatcherShutdownObserver(DispatcherShutdownObserver&&) = delete;

    zx_status_t WaitUntilShutdown() { return observer_.signal.Wait(zx::time::infinite()); }

    // Returns the observer that can be passed to |driver_runtime::Dispatcher::Create|.
    fdf_dispatcher_shutdown_observer_t* fdf_observer() { return &observer_.fdf_observer; }

    // Returns the dispatcher that was shutdown, nullptr if no shutdown event has occurred.
    fdf_dispatcher_t* shutdown_dispatcher() { return shutdown_dispatcher_; }

   private:
    struct ShutdownObserver {
      fdf_dispatcher_shutdown_observer_t fdf_observer;
      libsync::Completion signal;
    };

    static void DispatcherDestructedHandler(fdf_dispatcher_t* dispatcher,
                                            fdf_dispatcher_shutdown_observer_t* fdf_observer) {
      DispatcherShutdownObserver* observer =
          reinterpret_cast<DispatcherShutdownObserver*>(fdf_observer);
      observer->shutdown_dispatcher_ = dispatcher;
      observer->observer_.signal.Signal();
    }

    ShutdownObserver observer_;
    bool require_callback_;
    fdf_dispatcher_t* shutdown_dispatcher_ = nullptr;
  };

 private:
  int next_driver_ = 1;
};

#endif  // SRC_DEVICES_BIN_DRIVER_RUNTIME_RUNTIME_TEST_CASE_H_
