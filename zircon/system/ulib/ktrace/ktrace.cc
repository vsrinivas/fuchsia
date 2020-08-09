// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/tracing/kernel/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/ktrace/ktrace.h>
#include <lib/zircon-internal/ktrace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

const internal::KTraceSysCalls kKTraceSysCalls{
    .ktrace_control = zx_ktrace_control,
    .ktrace_read = zx_ktrace_read,
};

namespace ktrace {
class KTrace : public fuchsia::tracing::kernel::Controller,
               public fuchsia::tracing::kernel::Reader {
 public:
  explicit KTrace(zx::resource root_resource)
      : controller_(this), reader_(this), root_resource_(std::move(root_resource)) {}

  // fuchsia.tracing.kernel.Controller methods
  void Start(uint32_t group_mask, StartCallback callback) override;
  void Stop(StopCallback callback) override;
  void Rewind(RewindCallback callback) override;

  zx_status_t BindController(zx::channel channel, async_dispatcher_t* dispatcher);

  // fuchsia.tracing.kernel.Reader methods
  void ReadAt(uint64_t count, uint64_t offset, ReadAtCallback callback) override;
  void GetBytesWritten(GetBytesWrittenCallback callback) override;

  zx_status_t BindReader(zx::channel channel, async_dispatcher_t* dispatcher);

  void SetKTraceSysCall(internal::KTraceSysCalls sys_calls) { sys_calls_ = sys_calls; }

 private:
  fidl::Binding<fuchsia::tracing::kernel::Controller> controller_;
  fidl::Binding<fuchsia::tracing::kernel::Reader> reader_;

  zx::resource root_resource_;
  internal::KTraceSysCalls sys_calls_ = kKTraceSysCalls;
};

void KTrace::Start(uint32_t group_mask, StartCallback callback) {
  auto status =
      sys_calls_.ktrace_control(root_resource_.get(), KTRACE_ACTION_START, group_mask, nullptr);
  callback(status);
}

void KTrace::Stop(StopCallback callback) {
  auto status = sys_calls_.ktrace_control(root_resource_.get(), KTRACE_ACTION_STOP, 0, nullptr);
  callback(status);
}

void KTrace::Rewind(RewindCallback callback) {
  auto status = sys_calls_.ktrace_control(root_resource_.get(), KTRACE_ACTION_REWIND, 0, nullptr);
  callback(status);
}

zx_status_t KTrace::BindController(zx::channel channel, async_dispatcher_t* dispatcher) {
  return controller_.Bind(std::move(channel), dispatcher);
}

void KTrace::GetBytesWritten(GetBytesWrittenCallback callback) {
  size_t size = 0;
  auto status = sys_calls_.ktrace_read(root_resource_.get(), nullptr, 0, 0, &size);
  callback(status, size);
}

void KTrace::ReadAt(uint64_t count, uint64_t offset, ReadAtCallback callback) {
  size_t length;
  std::vector<uint8_t> buf(count);
  zx_status_t status =
      sys_calls_.ktrace_read(root_resource_.get(), buf.data(), offset, count, &length);
  buf.resize(length);
  callback(status, std::move(buf));
}

zx_status_t KTrace::BindReader(zx::channel channel, async_dispatcher_t* dispatcher) {
  return reader_.Bind(std::move(channel), dispatcher);
}

}  // namespace ktrace

// Method should be called only from test to override syscall
zx_status_t internal::OverrideKTraceSysCall(void* ctx, KTraceSysCalls sys_calls) {
  ktrace::KTrace* ktrace = static_cast<ktrace::KTrace*>(ctx);
  if (ktrace == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  ktrace->SetKTraceSysCall(sys_calls);
  return ZX_OK;
}

namespace {

zx_status_t Init(void** out_ctx) {
  zx::resource root_resource(static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(*out_ctx)));
  *out_ctx = new ktrace::KTrace(std::move(root_resource));
  return ZX_OK;
}

zx_status_t Connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                    zx_handle_t request) {
  if (!strcmp(service_name, fuchsia::tracing::kernel::Controller::Name_)) {
    ktrace::KTrace* ktrace = static_cast<ktrace::KTrace*>(ctx);
    return ktrace->BindController(zx::channel(request), dispatcher);
  }

  if (!strcmp(service_name, fuchsia::tracing::kernel::Reader::Name_)) {
    ktrace::KTrace* ktrace = static_cast<ktrace::KTrace*>(ctx);
    return ktrace->BindReader(zx::channel(request), dispatcher);
  }

  zx_handle_close(request);
  return ZX_ERR_NOT_SUPPORTED;
}

void Release(void* ctx) { delete static_cast<ktrace::KTrace*>(ctx); }

constexpr const char* kServices[] = {
    fuchsia::tracing::kernel::Controller::Name_,
    fuchsia::tracing::kernel::Reader::Name_,
    nullptr,
};

constexpr zx_service_ops_t kServiceOps = {
    .init = Init,
    .connect = Connect,
    .release = Release,
};

constexpr zx_service_provider_t kKTraceServiceProvider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = kServices,
    .ops = &kServiceOps,
};

}  // namespace

const zx_service_provider_t* ktrace_get_service_provider() { return &kKTraceServiceProvider; }
