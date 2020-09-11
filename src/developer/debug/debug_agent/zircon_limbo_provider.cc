// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_limbo_provider.h"

#include <zircon/status.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/zircon_exception_handle.h"
#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/developer/debug/debug_agent/zircon_thread_handle.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"

using namespace fuchsia::exception;

namespace debug_agent {

namespace {

LimboProvider::Record MetadataToRecord(ProcessExceptionMetadata metadata) {
  return ZirconLimboProvider::Record{
      .process = std::make_unique<ZirconProcessHandle>(std::move(*metadata.mutable_process())),
      .thread = std::make_unique<ZirconThreadHandle>(std::move(*metadata.mutable_thread()))};
}

}  // namespace

ZirconLimboProvider::ZirconLimboProvider(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(std::move(services)) {
  // Get the initial state of the hanging gets.
  ProcessLimboSyncPtr process_limbo;
  if (zx_status_t status = services_->Connect(process_limbo.NewRequest()); status != ZX_OK)
    return;

  // Check if the limbo is active.
  bool is_limbo_active = false;
  if (zx_status_t status = process_limbo->WatchActive(&is_limbo_active); status != ZX_OK)
    return;

  is_limbo_active_ = is_limbo_active;
  if (is_limbo_active_) {
    // Get the current set of process in exceptions.
    ProcessLimbo_WatchProcessesWaitingOnException_Result result;
    if (zx_status_t status = process_limbo->WatchProcessesWaitingOnException(&result);
        status != ZX_OK || result.is_err())
      return;

    // Add all the current exceptions.
    for (auto& exception : result.response().exception_list)
      limbo_[exception.info().process_koid] = MetadataToRecord(std::move(exception));
  }

  // Now that we were able to get the current state of the limbo, we move to an async binding.
  connection_.Bind(process_limbo.Unbind().TakeChannel());

  // |this| owns the connection, so it's guaranteed to outlive it.
  connection_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Got error from limbo: " << zx_status_get_string(status);
    limbo_.clear();
    is_limbo_active_ = false;
    connection_ = {};
  });

  WatchActive();
  WatchLimbo();

  valid_ = true;
}

void ZirconLimboProvider::WatchActive() {
  // |this| owns the connection, so it's guaranteed to outlive it.
  connection_->WatchActive([this](bool is_active) {
    if (!is_active)
      limbo_.clear();
    is_limbo_active_ = is_active;

    // Re-issue the hanging get.
    WatchActive();
  });
}

void ZirconLimboProvider::WatchLimbo() {
  connection_->WatchProcessesWaitingOnException(
      // |this| owns the connection, so we're guaranteed to outlive it.
      [this](ProcessLimbo_WatchProcessesWaitingOnException_Result result) {
        if (result.is_err())
          return;  // Limbo likely not enabled, give up.

        // The callback provides the full current list every time.
        RecordMap new_limbo;
        std::vector<zx_koid_t> new_exceptions;
        for (ProcessExceptionMetadata& exception : result.response().exception_list) {
          zx_koid_t process_koid = exception.info().process_koid;
          // Record if this is a new one we don't have yet.
          if (auto it = limbo_.find(process_koid); it == limbo_.end())
            new_exceptions.push_back(process_koid);

          new_limbo.insert({process_koid, MetadataToRecord(std::move(exception))});
        }

        limbo_ = std::move(new_limbo);

        if (on_enter_limbo_) {
          // Notify for the new exceptions.
          for (zx_koid_t process_koid : new_exceptions) {
            // Even though we added the exception above and expect it to be in limbo_, re-check
            // that we found it in case the callee consumed the exception out from under us.
            if (auto found = limbo_.find(process_koid); found != limbo_.end())
              on_enter_limbo_(found->second);
          }
        }

        // Re-issue the hanging get.
        WatchLimbo();
      });
}

bool ZirconLimboProvider::IsProcessInLimbo(zx_koid_t process_koid) const {
  const auto& records = GetLimboRecords();
  return records.find(process_koid) != records.end();
}

fitx::result<zx_status_t, ZirconLimboProvider::RetrievedException>
ZirconLimboProvider::RetrieveException(zx_koid_t process_koid) {
  ProcessLimboSyncPtr process_limbo;
  if (zx_status_t status = services_->Connect(process_limbo.NewRequest()); status != ZX_OK)
    return fitx::error(status);

  ProcessLimbo_RetrieveException_Result result = {};
  if (zx_status_t status = process_limbo->RetrieveException(process_koid, &result);
      status != ZX_OK) {
    return fitx::error(status);
  }

  if (result.is_err())
    return fitx::error(result.err());

  fuchsia::exception::ProcessException exception = result.response().ResultValue_();

  // Convert from the FIDL ExceptionInfo to the kernel zx_exception_info_t.
  const auto& source_info = exception.info();
  zx_exception_info_t info = {};
  info.pid = source_info.process_koid;
  info.tid = source_info.thread_koid;
  info.type = static_cast<zx_excp_type_t>(source_info.type);

  RetrievedException retrieved;
  retrieved.process =
      std::make_unique<ZirconProcessHandle>(std::move(*exception.mutable_process()));
  retrieved.thread = std::make_unique<ZirconThreadHandle>(std::move(*exception.mutable_thread()));
  retrieved.exception =
      std::make_unique<ZirconExceptionHandle>(std::move(*exception.mutable_exception()), info);

  return fitx::ok(std::move(retrieved));
}

zx_status_t ZirconLimboProvider::ReleaseProcess(zx_koid_t process_koid) {
  ProcessLimboSyncPtr process_limbo;
  if (zx_status_t status = services_->Connect(process_limbo.NewRequest()); status != ZX_OK)
    return status;

  ProcessLimbo_ReleaseProcess_Result result;
  if (zx_status_t status = process_limbo->ReleaseProcess(process_koid, &result);
      status != ZX_OK || result.is_err())
    return status;

  limbo_.erase(process_koid);
  return ZX_OK;
}

}  // namespace debug_agent
