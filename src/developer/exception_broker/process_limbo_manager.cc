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

  // Notify the handlers of the new list of processes in limbo.
  PruneStaleHandlers(&handlers_);
  for (auto& handler : handlers_) {
    auto limbo_list = ListProcessesInLimbo();
    handler->LimboChanged(std::move(limbo_list));
  }
}

void ProcessLimboManager::AddHandler(fxl::WeakPtr<ProcessLimboHandler> handler) {
  handlers_.push_back(std::move(handler));
}

std::vector<ProcessExceptionMetadata> ProcessLimboManager::ListProcessesInLimbo() {
  std::vector<ProcessExceptionMetadata> exceptions;

  size_t max_size =
      limbo_.size() <= MAX_EXCEPTIONS_PER_CALL ? limbo_.size() : MAX_EXCEPTIONS_PER_CALL;
  exceptions.reserve(max_size);

  // The new rights of the handles we're going to duplicate.
  zx_rights_t rights = ZX_RIGHT_READ | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_TRANSFER;
  for (const auto& [process_koid, limbo_exception] : limbo_) {
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

  return exceptions;
}

bool ProcessLimboManager::SetActive(bool active) {
  // Ignore if no change.
  if (active == active_)
    return false;
  active_ = active;

  // Notify the handlers of the new activa state.
  PruneStaleHandlers(&handlers_);
  for (auto& handler : handlers_) {
    handler->ActiveStateChanged(active);
  }

  return true;
}

// ProcessLimboHandler -----------------------------------------------------------------------------

ProcessLimboHandler::ProcessLimboHandler(fxl::WeakPtr<ProcessLimboManager> limbo_manager)
    : limbo_manager_(std::move(limbo_manager)), weak_factory_(this) {}

fxl::WeakPtr<ProcessLimboHandler> ProcessLimboHandler::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void ProcessLimboHandler::ActiveStateChanged(bool active) {
  if (!is_active_callback_) {
    // Reset the WatchActive state as the state is different from the last time the get was called.
    watch_active_dirty_bit_ = true;
  } else {
    is_active_callback_(active);
    is_active_callback_ = {};
    watch_active_dirty_bit_ = false;
  }

  // If there is a limbo call waiting, we tell them that it's canceled.
  if (!active) {
    if (watch_limbo_callback_) {
      watch_limbo_callback_(fit::error(ZX_ERR_CANCELED));
      watch_limbo_callback_ = {};
      watch_limbo_dirty_bit_ = false;
    } else {
      watch_limbo_dirty_bit_ = true;
    }
  }
}

void ProcessLimboHandler::LimboChanged(std::vector<ProcessExceptionMetadata> limbo_list) {
  if (!watch_limbo_callback_) {
    // Reset the hanging get state as the state is different from the first time the get was called.
    watch_limbo_dirty_bit_ = true;
    return;
  }

  watch_limbo_callback_(fit::ok(std::move(limbo_list)));
  watch_limbo_callback_ = {};
  watch_limbo_dirty_bit_ = false;
}

void ProcessLimboHandler::WatchActive(WatchActiveCallback cb) {
  if (watch_active_dirty_bit_) {
    watch_active_dirty_bit_ = false;

    bool is_active = !!limbo_manager_ ? limbo_manager_->active() : false;
    cb(is_active);
    return;
  }

  // We store the latest callback for when the active state changes.
  is_active_callback_ = std::move(cb);
}

void ProcessLimboHandler::WatchProcessesWaitingOnException(
    WatchProcessesWaitingOnExceptionCallback cb) {
  if (!limbo_manager_) {
    cb(fit::error(ZX_ERR_BAD_STATE));
    return;
  }

  if (!limbo_manager_->active()) {
    cb(fit::error(ZX_ERR_UNAVAILABLE));
    return;
  }

  if (watch_limbo_dirty_bit_) {
    watch_limbo_dirty_bit_ = false;

    auto processes = limbo_manager_->ListProcessesInLimbo();
    cb(fit::ok(std::move(processes)));
    return;
  }

  // Store the latest callback for when the processes enter the limbo.
  watch_limbo_callback_ = std::move(cb);
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
