// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status_watcher.h"

#include <zircon/status.h>

#include "log.h"

namespace network::internal {

namespace {

bool StatusEquals(const port_status_t& a, const port_status_t& b) {
  return a.flags == b.flags && a.mtu == b.mtu;
}

}  // namespace

StatusWatcher::StatusWatcher(uint32_t max_queue) : max_queue_(max_queue) {
  if (max_queue_ == 0) {
    max_queue_ = 1;
  } else if (max_queue_ > netdev::wire::kMaxStatusBuffer) {
    max_queue_ = netdev::wire::kMaxStatusBuffer;
  }
}

zx_status_t StatusWatcher::Bind(async_dispatcher_t* dispatcher,
                                fidl::ServerEnd<netdev::StatusWatcher> channel,
                                fit::callback<void(StatusWatcher*)> closed_callback) {
  fbl::AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT(!binding_.has_value());
  binding_ = fidl::BindServer(
      dispatcher, std::move(channel), this,
      [](StatusWatcher* closed, fidl::UnbindInfo info,
         fidl::ServerEnd<fuchsia_hardware_network::StatusWatcher> /*unused*/) {
        LOGF_TRACE("network-device: watcher closed: %s", info.FormatDescription().c_str());
        fbl::AutoLock lock(&closed->lock_);
        closed->binding_.reset();
        if (closed->pending_txn_.has_value()) {
          closed->pending_txn_->Close(ZX_ERR_CANCELED);
          closed->pending_txn_.reset();
        }
        if (closed->closed_cb_) {
          lock.release();
          closed->closed_cb_(closed);
        }
      });
  closed_cb_ = std::move(closed_callback);
  return ZX_OK;
}

void StatusWatcher::Unbind() {
  fbl::AutoLock lock(&lock_);
  if (pending_txn_.has_value()) {
    pending_txn_->Close(ZX_ERR_CANCELED);
    pending_txn_.reset();
  }

  if (binding_.has_value()) {
    binding_->Unbind();
    binding_.reset();
  }
}

StatusWatcher::~StatusWatcher() {
  ZX_ASSERT_MSG(!pending_txn_.has_value(),
                "Tried to destroy StatusWatcher with a pending transaction");
  ZX_ASSERT_MSG(!binding_.has_value(), "Tried to destroy StatusWatcher without unbinding");
}

void StatusWatcher::WatchStatus(WatchStatusRequestView request,
                                WatchStatusCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  if (queue_.empty()) {
    if (pending_txn_.has_value()) {
      if (last_observed_.has_value()) {
        // Complete the last pending transaction with the old value and retain the new completer as
        // an async transaction.
        WithWireStatus(
            [completer = std::move(std::exchange(pending_txn_, completer.ToAsync()).value())](
                netdev::wire::PortStatus wire_status) mutable { completer.Reply(wire_status); },
            last_observed_.value());
      } else {
        // If we already have a pending transaction that hasn't been resolved and we don't have a
        // last observed value to give to it (meaning whoever created `StatusWatcher` scheduled it
        // without ever pushing any status information), we have no choice but to close the newer
        // completer.
        completer.Close(ZX_ERR_BAD_STATE);
      }
    } else {
      pending_txn_ = completer.ToAsync();
    }
  } else {
    const port_status_t status = queue_.front();
    queue_.pop();
    WithWireStatus(
        [&completer](netdev::wire::PortStatus wire_status) { completer.Reply(wire_status); },
        status);
    last_observed_ = status;
  }
}

void StatusWatcher::PushStatus(const port_status_t& status) {
  fbl::AutoLock lock(&lock_);
  std::optional<port_status_t> tail;
  if (queue_.empty()) {
    tail = last_observed_;
  } else {
    tail = queue_.back();
  }
  if (tail.has_value() && StatusEquals(tail.value(), status)) {
    // ignore if no change is observed
    return;
  }

  if (pending_txn_.has_value() && queue_.empty()) {
    WithWireStatus(
        [completer = std::move(std::exchange(pending_txn_, std::nullopt).value())](
            netdev::wire::PortStatus wire_status) mutable { completer.Reply(wire_status); },
        status);
    last_observed_ = status;
  } else {
    queue_.push(status);
    // limit the queue to max_queue_
    if (queue_.size() > max_queue_) {
      queue_.pop();
    }
  }
}

}  // namespace network::internal
