// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_INTERNAL_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_INTERNAL_H_

#include <lib/fdf/internal.h>
#include <lib/fit/function.h>

#include <mutex>

namespace fdf_internal {

// For shutting down all dispatchers owned by a driver.
// This class is thread-safe.
class DriverShutdown : public fdf_internal_driver_shutdown_observer_t {
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
  // This may be called from any thread.
  //
  // Returns ZX_OK if successful and |shutdown_handler| will be notified.
  // Returns ZX_ERR_INVALID_ARGS if no driver matching |driver| was found.
  // Returns ZX_ERR_BAD_STATE if a driver shutdown observer was already registered.
  zx_status_t Begin(const void* driver, Handler shutdown_handler);

  // If |Begin| was called successfully, you must keep this alive until the |Handler|
  // has been called.
  //
  // This may be destructed from any thread.
  ~DriverShutdown();

 private:
  static void CallHandler(const void* driver, fdf_internal_driver_shutdown_observer_t* observer);

  std::mutex lock_;
  const void* driver_ __TA_GUARDED(lock_) = nullptr;
  Handler handler_ __TA_GUARDED(lock_);
};

}  // namespace fdf_internal

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_INTERNAL_H_
