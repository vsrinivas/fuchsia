// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/limbo_provider.h"

#include <zircon/status.h>

#include "src/developer/debug/debug_agent/object_provider.h"

using namespace fuchsia::exception;

namespace debug_agent {

// LimboProvider -----------------------------------------------------------------------------------

LimboProvider::LimboProvider(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(std::move(services)) {}
LimboProvider::~LimboProvider() = default;

zx_status_t LimboProvider::Init() {
  // We get the initial state of the hanging gets.
  ProcessLimboSyncPtr process_limbo;
  zx_status_t status = services_->Connect(process_limbo.NewRequest());
  if (status != ZX_OK)
    return status;

  // Check if the limbo is active.
  bool is_limbo_active = false;
  status = process_limbo->WatchActive(&is_limbo_active);
  if (status != ZX_OK)
    return status;

  is_limbo_active_ = is_limbo_active;
  if (is_limbo_active_) {
    // Get the current set of process in exceptions.
    ProcessLimbo_WatchProcessesWaitingOnException_Result result;
    status = process_limbo->WatchProcessesWaitingOnException(&result);
    if (status != ZX_OK)
      return status;

    if (result.is_err())
      return result.err();

    // Add to the exceptions.
    for (auto& exception : result.response().exception_list) {
      zx_koid_t process_koid = exception.info().process_koid;
      limbo_[process_koid] = std::move(exception);
    }
  }

  // Now that we were able to get the current state of the limbo, we move to an async binding.
  connection_.Bind(process_limbo.Unbind().TakeChannel());

  // |this| owns the connection, so it's guaranteed to outlive it.
  connection_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Got error on limbo: " << zx_status_get_string(status);
    Reset();
  });

  WatchActive();
  WatchLimbo();

  valid_ = true;
  return ZX_OK;
}

void LimboProvider::Reset() {
  valid_ = false;
  limbo_.clear();
  is_limbo_active_ = false;
  connection_ = {};
}

void LimboProvider::WatchActive() {
  // |this| owns the connection, so it's guaranteed to outlive it.
  connection_->WatchActive([this](bool is_active) {
    if (!is_active)
      limbo_.clear();
    is_limbo_active_ = is_active;

    // Re-issue the hanging get.
    WatchActive();
  });
}

bool LimboProvider::Valid() const { return valid_; }

const std::map<zx_koid_t, fuchsia::exception::ProcessExceptionMetadata>& LimboProvider::Limbo()
    const {
  return limbo_;
}

// WatchLimbo --------------------------------------------------------------------------------------

namespace {

ProcessExceptionMetadata DuplicateException(const ProcessExceptionMetadata& exception) {
  ProcessExceptionMetadata result = {};
  result.set_info(exception.info());

  if (exception.has_process()) {
    zx::process process;
    exception.process().duplicate(ZX_RIGHT_SAME_RIGHTS, &process);
    result.set_process(std::move(process));
  }

  if (exception.has_thread()) {
    zx::thread thread;
    exception.thread().duplicate(ZX_RIGHT_SAME_RIGHTS, &thread);
    result.set_thread(std::move(thread));
  }

  return result;
}

}  // namespace

void LimboProvider::WatchLimbo() {
  connection_->WatchProcessesWaitingOnException(
      // |this| owns the connection, so it's guaranteed to outlive it.
      [this](ProcessLimbo_WatchProcessesWaitingOnException_Result result) {
        if (result.is_err()) {
          FXL_LOG(ERROR) << "Got error waiting on limbo: " << zx_status_get_string(result.err());
        } else {
          // Add the exceptions to the limbo.
          std::vector<fuchsia::exception::ProcessExceptionMetadata> new_exceptions;
          for (auto& exception : result.response().exception_list) {
            zx_koid_t process_koid = exception.info().process_koid;

            auto [it, inserted] = limbo_.insert({process_koid, std::move(exception)});

            // Only track new processes if we're going to inform it through a callback.
            if (on_enter_limbo_ && inserted)
              new_exceptions.push_back(DuplicateException(it->second));
          }

          if (on_enter_limbo_)
            on_enter_limbo_(std::move(new_exceptions));
        }

        // Re-issue the hanging get.
        WatchLimbo();
      });
}

zx_status_t LimboProvider::RetrieveException(zx_koid_t process_koid,
                                             fuchsia::exception::ProcessException* out) {
  ProcessLimboSyncPtr process_limbo;
  if (zx_status_t status = services_->Connect(process_limbo.NewRequest()); status != ZX_OK)
    return status;

  ProcessLimbo_RetrieveException_Result result = {};
  if (zx_status_t status = process_limbo->RetrieveException(process_koid, &result);
      status != ZX_OK) {
    return status;
  }

  if (result.is_err())
    return result.err();

  *out = std::move(result.response().ResultValue_());
  return ZX_OK;
}

zx_status_t LimboProvider::ReleaseProcess(zx_koid_t process_koid) {
  ProcessLimboSyncPtr process_limbo;
  if (zx_status_t status = services_->Connect(process_limbo.NewRequest()); status != ZX_OK)
    return status;

  ProcessLimbo_ReleaseProcess_Result result;
  if (zx_status_t status = process_limbo->ReleaseProcess(process_koid, &result); status != ZX_OK)
    return status;

  if (result.is_err())
    return result.err();

  return ZX_OK;
}

}  // namespace debug_agent
