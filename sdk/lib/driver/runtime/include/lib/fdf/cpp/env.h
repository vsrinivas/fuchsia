// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ENV_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ENV_H_

#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/env.h>
#include <lib/fit/function.h>

#include <mutex>

namespace fdf_env {

class DispatcherBuilder {
 public:
  // Same as |fdf::Dispatcher::Create| but allows setting the driver owner for the dispatcher.
  //
  // |driver| is an opaque pointer to the driver object. It will be used to uniquely identify
  // the driver.
  static zx::result<fdf::Dispatcher> CreateWithOwner(
      const void* driver, uint32_t options, cpp17::string_view name,
      fdf::Dispatcher::ShutdownHandler shutdown_handler, cpp17::string_view scheduler_role = {}) {
    // We need to create an additional shutdown context in addition to the fdf::Dispatcher
    // object, as the fdf::Dispatcher may be destructed before the shutdown handler
    // is called. This can happen if the raw pointer is released from the fdf::Dispatcher.
    auto dispatcher_shutdown_context =
        std::make_unique<fdf::Dispatcher::DispatcherShutdownContext>(std::move(shutdown_handler));
    fdf_dispatcher_t* dispatcher;
    zx_status_t status = fdf_env_dispatcher_create_with_owner(
        driver, options, name.data(), name.size(), scheduler_role.data(), scheduler_role.size(),
        dispatcher_shutdown_context->observer(), &dispatcher);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    dispatcher_shutdown_context.release();
    return zx::ok(fdf::Dispatcher(dispatcher));
  }
};

// For shutting down all dispatchers owned by a driver.
class DriverShutdown : public fdf_env_driver_shutdown_observer_t {
 public:
  // Called when the asynchronous shutdown of all dispatchers owned by |driver| completes.
  using Handler = fit::callback<void(const void* driver)>;

  DriverShutdown();

  // Asynchronously shuts down all dispatchers owned by |driver|.
  // |shutdown_handler| will be notified once shutdown completes. This is guaranteed to be
  // after all the dispatcher's shutdown observers have been called, and will be running
  // on the thread of the final dispatcher which has been shutdown.
  //
  // While a driver is shutting down, no new dispatchers can be created by the driver.
  // If this succeeds, you must keep the |shutdown_handler| object alive until the
  // |shutdown_handler| is notified once the last dispatcher completes shutting down.
  //
  // # Thread safety
  //
  // This class is thread-safe.
  //
  // # Errors
  //
  // ZX_ERR_INVALID_ARGS: No driver matching |driver| was found.
  //
  // ZX_ERR_BAD_STATE: A driver shutdown observer was already registered.
  zx_status_t Begin(const void* driver, Handler shutdown_handler);

  // If |Begin| was called successfully, you must keep this alive until the |Handler|
  // has been called.
  //
  // This may be destructed from any thread.
  ~DriverShutdown();

 private:
  static void CallHandler(const void* driver, fdf_env_driver_shutdown_observer_t* observer);

  std::mutex lock_;
  const void* driver_ __TA_GUARDED(lock_) = nullptr;
  Handler handler_ __TA_GUARDED(lock_);
};

}  // namespace fdf_env

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ENV_H_
