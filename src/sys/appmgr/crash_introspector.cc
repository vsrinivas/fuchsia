// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/crash_introspector.h"

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include "fbl/auto_call.h"
#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fitx/result.h"
#include "src/lib/fsl/handles/object_info.h"

namespace component {

using fuchsia::sys::internal::CrashIntrospect_FindComponentByThreadKoid_Response;
using fuchsia::sys::internal::CrashIntrospect_FindComponentByThreadKoid_Result;

const uint8_t kDefaultThreadCacheTimeoutSec = 10;

CrashIntrospector::CrashIntrospector() : weak_ptr_factory_(this) {}

CrashIntrospector::~CrashIntrospector() = default;

void CrashIntrospector::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::internal::CrashIntrospect> request) {
  bindings_.AddBinding(this, std::move(request));
}

void CrashIntrospector::FindComponentByThreadKoid(zx_koid_t thread_koid,
                                                  FindComponentByThreadKoidCallback callback) {
  CrashIntrospect_FindComponentByThreadKoid_Result result;
  auto status = RemoveThreadFromCache(thread_koid);
  if (status.is_ok()) {
    CrashIntrospect_FindComponentByThreadKoid_Response response;
    response.component_info = std::move(status.value());
    result.set_response(std::move(response));
  } else {
    result.set_err(ZX_ERR_NOT_FOUND);
  }

  callback(std::move(result));
}

void CrashIntrospector::RegisterJob(const zx::job& job,
                                    fuchsia::sys::internal::SourceIdentity component_info) {
  zx::channel exception_channel;
  job.create_exception_channel(0, &exception_channel);
  auto monitor = std::make_unique<CrashMonitor>(
      weak_ptr_factory_.GetWeakPtr(), std::move(exception_channel), std::move(component_info));
  monitors_.emplace(monitor.get(), std::move(monitor));
}

std::unique_ptr<CrashIntrospector::CrashMonitor> CrashIntrospector::ExtractMonitor(
    const CrashIntrospector::CrashMonitor* monitor) {
  auto it = monitors_.find(monitor);
  if (it == monitors_.end()) {
    return nullptr;
  }
  auto obj = std::move(it->second);
  monitors_.erase(it);
  return obj;
}

fitx::result<bool, fuchsia::sys::internal::SourceIdentity> CrashIntrospector::RemoveThreadFromCache(
    zx_koid_t thread_koid) {
  auto it = thread_cache_.find(thread_koid);
  if (it == thread_cache_.end()) {
    return fitx::error(false);
  }
  auto obj = std::move(it->second);
  // already removed, cancel auto removal task if pending
  obj.first->Cancel();
  thread_cache_.erase(it);
  return fitx::ok(std::move(obj.second));
}

void CrashIntrospector::AddThreadToCache(
    const zx::thread& thread, const fuchsia::sys::internal::SourceIdentity& component_info) {
  const auto thread_koid = fsl::GetKoid(thread.get());
  if (thread_cache_.count(thread_koid) > 0) {
    FX_LOGS(ERROR) << "Thread " << thread_koid << " already in map.";
    return;
  }

  auto timeout_task = std::make_unique<async::TaskClosure>([this, thread_koid]() {
    auto result = RemoveThreadFromCache(thread_koid);
    // result will die at end of this statement
  });

  timeout_task->PostDelayed(async_get_default_dispatcher(), zx::sec(kDefaultThreadCacheTimeoutSec));

  thread_cache_.emplace(thread_koid,
                        std::make_pair(std::move(timeout_task), fidl::Clone(component_info)));
}

CrashIntrospector::CrashMonitor::CrashMonitor(fxl::WeakPtr<CrashIntrospector> introspector,
                                              zx::channel exception_channel,
                                              fuchsia::sys::internal::SourceIdentity component_info)
    : introspector_(std::move(introspector)),
      component_info_(std::move(component_info)),
      exception_channel_(std::move(exception_channel)),
      wait_(this, exception_channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED) {
  wait_.Begin(async_get_default_dispatcher());
}

void CrashIntrospector::CrashMonitor::CrashHandler(async_dispatcher_t* dispatcher,
                                                   async::WaitBase* wait, zx_status_t status,
                                                   const zx_packet_signal* signal) {
  FX_CHECK(status == ZX_OK) << "status: " << status;

  if (signal->observed & ZX_CHANNEL_READABLE) {
    // wait for next signal
    auto run_again = fbl::MakeAutoCall([&wait, &dispatcher] { wait->Begin(dispatcher); });
    zx_exception_info_t info;
    zx::exception exception;
    if (const zx_status_t status = exception_channel_.read(
            0, &info, exception.reset_and_get_address(), sizeof(info), 1, nullptr, nullptr);
        status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to read from the exception channel: " << status;
      return;
    }
    zx::thread thread;
    if (const zx_status_t status = exception.get_thread(&thread); status != ZX_OK) {
      FX_LOGS(ERROR) << "Could not get thread for exception: " << status;

      return;
    }

    if (introspector_) {
      introspector_->AddThreadToCache(thread, component_info_);
    } else {
      run_again.cancel();
      // parent is already dead
      wait_.Cancel();
    }
    return;
  }
  FX_CHECK(signal->observed & ZX_CHANNEL_PEER_CLOSED) << "signal observed: " << signal->observed;
  // job died, stop monitoring
  wait_.Cancel();
  if (introspector_) {
    auto self = introspector_->ExtractMonitor(this);
    // |self| will die when this block finishes
  }
  // this object will die when this function returns
}

CrashIntrospector::CrashMonitor::~CrashMonitor() = default;

}  // namespace component
