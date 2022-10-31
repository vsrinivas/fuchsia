// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/env.h>
#include <zircon/assert.h>

namespace fdf_env {

DriverShutdown::DriverShutdown() : fdf_env_driver_shutdown_observer_t{CallHandler} {}

zx_status_t DriverShutdown::Begin(const void* driver, Handler shutdown_handler) {
  // Since calls to the driver runtime are non-reentrant we can safely hold the lock.
  std::lock_guard<std::mutex> lock(lock_);

  if (handler_) {
    return ZX_ERR_BAD_STATE;
  }
  driver_ = driver;
  handler_ = std::move(shutdown_handler);
  zx_status_t status = fdf_env_shutdown_dispatchers_async(driver_, this);
  if (status != ZX_OK) {
    driver_ = nullptr;
    handler_ = nullptr;
    return status;
  }
  return ZX_OK;
}

DriverShutdown::~DriverShutdown() {
  std::lock_guard<std::mutex> lock(lock_);
  ZX_ASSERT(!handler_);
}

// static
void DriverShutdown::CallHandler(const void* driver, fdf_env_driver_shutdown_observer_t* observer) {
  Handler handler;
  {
    auto self = static_cast<DriverShutdown*>(observer);
    std::lock_guard<std::mutex> lock(self->lock_);
    // Move the handler to the stack prior to calling.
    handler = std::move(self->handler_);
  }
  ZX_ASSERT(handler);
  handler(driver);
}

}  // namespace fdf_env
