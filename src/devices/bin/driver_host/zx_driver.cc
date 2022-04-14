// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host/zx_driver.h"

#include <lib/sync/cpp/completion.h>

#include "src/devices/bin/driver_host/driver.h"

zx_status_t zx_driver::InitOp(const fbl::RefPtr<Driver>& driver) {
  libsync::Completion completion;
  zx_status_t status;

  async::PostTask(driver->dispatcher()->async_dispatcher(), [&]() {
    status = ops_->init(&ctx_);
    completion.Signal();
  });

  completion.Wait();
  return status;
}

zx_status_t zx_driver::BindOp(internal::BindContext* bind_context,
                              const fbl::RefPtr<Driver>& driver,
                              const fbl::RefPtr<zx_device_t>& device) const {
  fbl::StringBuffer<32> trace_label;
  trace_label.AppendPrintf("%s:bind", name_);
  TRACE_DURATION("driver_host:driver-hooks", trace_label.data());

  libsync::Completion completion;
  zx_status_t status;

  async::PostTask(driver->dispatcher()->async_dispatcher(), [&]() {
    internal::set_bind_context(bind_context);
    status = ops_->bind(ctx_, device.get());
    internal::set_bind_context(nullptr);
    completion.Signal();
  });

  completion.Wait();
  return status;
}

zx_status_t zx_driver::CreateOp(internal::CreationContext* creation_context,
                                const fbl::RefPtr<Driver>& driver,
                                const fbl::RefPtr<zx_device_t>& parent, const char* name,
                                const char* args, zx_handle_t rpc_channel) const {
  libsync::Completion completion;
  zx_status_t status;

  async::PostTask(driver->dispatcher()->async_dispatcher(), [&]() {
    internal::set_creation_context(creation_context);
    status = ops_->create(ctx_, parent.get(), name, args, rpc_channel);
    internal::set_creation_context(nullptr);
    completion.Signal();
  });

  completion.Wait();
  return status;
}

void zx_driver::ReleaseOp(const fbl::RefPtr<Driver>& driver) const {
  libsync::Completion completion;
  async::PostTask(driver->dispatcher()->async_dispatcher(), [&]() {
    // TODO(kulakowski/teisenbe) Consider poisoning the ops_ table on release.
    ops_->release(ctx_);
    completion.Signal();
  });
  completion.Wait();
}

bool zx_driver::RunUnitTestsOp(const fbl::RefPtr<zx_device_t>& parent,
                               const fbl::RefPtr<Driver>& driver, zx::channel test_output) const {
  libsync::Completion completion;
  bool result;

  async::PostTask(driver->dispatcher()->async_dispatcher(), [&]() {
    result = ops_->run_unit_tests(ctx_, parent.get(), test_output.release());
    completion.Signal();
  });

  completion.Wait();
  return result;
}
