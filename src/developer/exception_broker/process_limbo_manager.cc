// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/exception_broker/process_limbo_manager.h"

#include "src/lib/syslog/cpp/logger.h"

namespace fuchsia {
namespace exception {

namespace {

// Removes all stale weak pointers from the handler list.
void PruneStaleHandlers(std::vector<fxl::WeakPtr<ProcessLimboHandler>>* handlers) {
  // We only move active handlers to the new list.
  std::vector<fxl::WeakPtr<ProcessLimboHandler>> new_handlers;
  for (auto& handler : *handlers) {
    if (handler)
      new_handlers.push_back(std::move(handler));
  }

  *handlers = std::move(new_handlers);
}

}  // namespace

ProcessLimboManager::ProcessLimboManager() : weak_factory_(this) {}

fxl::WeakPtr<ProcessLimboManager> ProcessLimboManager::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void ProcessLimboManager::AddToLimbo(ProcessException process_exception) {
  limbo_[process_exception.info().process_koid] = std::move(process_exception);
}

void ProcessLimboManager::AddHandler(fxl::WeakPtr<ProcessLimboHandler> handler) {
  handlers_.push_back(std::move(handler));
}

bool ProcessLimboManager::SetActive(bool active) {
  // Ignore if no change.
  if (active == active_)
    return false;
  active_ = active;

  PruneStaleHandlers(&handlers_);

  for (auto& handler : handlers_) {
    handler->ActiveStateChanged(active);
  }

  return true;
}

// ProcessLimboHandler -----------------------------------------------------------------------------

ProcessLimboHandler::ProcessLimboHandler(fxl::WeakPtr<ProcessLimboManager> limbo_manager)
    : limbo_manager_(std::move(limbo_manager)), weak_factory_(this) {}
ProcessLimboHandler::~ProcessLimboHandler() = default;

fxl::WeakPtr<ProcessLimboHandler> ProcessLimboHandler::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void ProcessLimboHandler::ActiveStateChanged(bool state) {
  if (!is_active_callback_)
    return;
  is_active_callback_(state);
  is_active_callback_ = {};
}

void ProcessLimboHandler::WatchActive(WatchActiveCallback cb) {
  if (is_first_watch_active_call_) {
    is_first_watch_active_call_ = false;

    bool is_active = !!limbo_manager_ ? limbo_manager_->active() : false;
    cb(is_active);
    return;
  }

  // We store the latest callback for when the active status changes.
  is_active_callback_ = std::move(cb);
}

void ProcessLimboHandler::ListProcessesWaitingOnException(
    ListProcessesWaitingOnExceptionCallback cb) {
  if (!limbo_manager_) {
    cb({});
    return;
  }

  std::vector<ProcessExceptionMetadata> exceptions;

  auto& limbo = limbo_manager_->limbo_;

  size_t max_size =
      limbo.size() <= MAX_EXCEPTIONS_PER_CALL ? limbo.size() : MAX_EXCEPTIONS_PER_CALL;
  exceptions.reserve(max_size);

  // The new rights of the handles we're going to duplicate.
  zx_rights_t rights = ZX_RIGHT_READ | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_TRANSFER;
  for (const auto& [process_koid, limbo_exception] : limbo) {
    ProcessExceptionMetadata metadata = {};

    zx::process process;
    if (auto res = limbo_exception.process().duplicate(rights, &process); res != ZX_OK) {
      FX_PLOGS(ERROR, res) << "Could not duplicate process handle.";
      continue;
    }

    zx::thread thread;
    if (auto res = limbo_exception.thread().duplicate(rights, &thread); res != ZX_OK) {
      FX_PLOGS(ERROR, res) << "Could not duplicate thread handle.";
      continue;
    }

    metadata.set_info(limbo_exception.info());
    metadata.set_process(std::move(process));
    metadata.set_thread(std::move(thread));

    exceptions.push_back(std::move(metadata));

    if (exceptions.size() >= MAX_EXCEPTIONS_PER_CALL)
      break;
  }

  cb(std::move(exceptions));
}

void ProcessLimboHandler::RetrieveException(zx_koid_t process_koid, RetrieveExceptionCallback cb) {
  if (!limbo_manager_) {
    cb(fit::error(ZX_ERR_UNAVAILABLE));
    return;
  }

  ProcessLimbo_RetrieveException_Result result;

  auto& limbo = limbo_manager_->limbo_;

  auto it = limbo.find(process_koid);
  if (it == limbo.end()) {
    FX_LOGS(WARNING) << "Could not find process " << process_koid << " in limbo.";
    cb(fit::error(ZX_ERR_NOT_FOUND));
  } else {
    auto res = fit::ok(std::move(it->second));
    limbo.erase(it);
    cb(std::move(res));
  }
}

void ProcessLimboHandler::ReleaseProcess(zx_koid_t process_koid, ReleaseProcessCallback cb) {
  if (!limbo_manager_) {
    cb(fit::error(ZX_ERR_UNAVAILABLE));
    return;
  }

  auto& limbo = limbo_manager_->limbo_;

  auto it = limbo.find(process_koid);
  if (it == limbo.end()) {
    return cb(fit::error(ZX_ERR_NOT_FOUND));
  }

  limbo.erase(it);
  return cb(fit::ok());
}

}  // namespace exception
}  // namespace fuchsia
