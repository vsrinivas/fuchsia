// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/sync_watcher_set.h"

#include <lib/fit/function.h>

namespace ledger {
namespace {
SyncState ConvertToSyncState(sync_coordinator::DownloadSyncState download) {
  switch (download) {
    case sync_coordinator::DOWNLOAD_IDLE:
      return SyncState::IDLE;
    case sync_coordinator::DOWNLOAD_PENDING:
      return SyncState::PENDING;
    case sync_coordinator::DOWNLOAD_IN_PROGRESS:
      return SyncState::IN_PROGRESS;
    case sync_coordinator::DOWNLOAD_ERROR:
      return SyncState::ERROR;
  }
}

SyncState ConvertToSyncState(sync_coordinator::UploadSyncState upload) {
  switch (upload) {
    case sync_coordinator::UPLOAD_IDLE:
      return SyncState::IDLE;
    case sync_coordinator::UPLOAD_PENDING:
      return SyncState::PENDING;
    case sync_coordinator::UPLOAD_IN_PROGRESS:
      return SyncState::IN_PROGRESS;
    case sync_coordinator::UPLOAD_ERROR:
      return SyncState::ERROR;
  }
}

bool operator==(const sync_coordinator::SyncStateWatcher::SyncStateContainer& lhs,
                const sync_coordinator::SyncStateWatcher::SyncStateContainer& rhs) {
  return std::tie(lhs.download, lhs.upload) == std::tie(rhs.download, rhs.upload);
}
}  // namespace

class SyncWatcherSet::SyncWatcherContainer : public sync_coordinator::SyncStateWatcher {
 public:
  explicit SyncWatcherContainer(SyncWatcherPtr watcher) : watcher_(std::move(watcher)) {}
  SyncWatcherContainer(const SyncWatcherContainer&) = delete;
  SyncWatcherContainer& operator=(const SyncWatcherContainer&) = delete;

  ~SyncWatcherContainer() override = default;

  void Start(SyncStateContainer base_state) {
    pending_ = base_state;
    Send();
  }

  void Notify(SyncStateContainer sync_state) override {
    if (sync_state == pending_) {
      return;
    }
    pending_ = sync_state;

    SendIfPending();
  }

  void SetOnDiscardable(fit::closure on_discardable) {
    if (on_discardable) {
      watcher_.set_error_handler([this, callback = std::move(on_discardable)](zx_status_t status) {
        watcher_.Unbind();
        callback();
      });
    }
  }

  bool IsDiscardable() const { return !watcher_.is_bound(); }

 private:
  void SendIfPending() {
    if (!watcher_ || notification_in_progress_ || last_ == pending_) {
      return;
    }
    Send();
  }

  void Send() {
    notification_in_progress_ = true;
    last_ = pending_;
    watcher_->SyncStateChanged(ConvertToSyncState(last_.download), ConvertToSyncState(last_.upload),
                               [this]() {
                                 notification_in_progress_ = false;
                                 SendIfPending();
                               });
  }

  // fidl interface to the client.
  SyncWatcherPtr watcher_;

  // True if a notification has been sent but not acknowledged by the client.
  bool notification_in_progress_ = false;
  // pending_ contains the next synchronization state to send to the watcher,
  // or the current one if no notification is currently in progress
  SyncStateContainer pending_;
  // last_ contains the last sent notification.
  SyncStateContainer last_;
};

SyncWatcherSet::SyncWatcherSet(async_dispatcher_t* dispatcher) : watchers_(dispatcher) {}

SyncWatcherSet::~SyncWatcherSet() = default;

void SyncWatcherSet::AddSyncWatcher(fidl::InterfaceHandle<SyncWatcher> watcher) {
  SyncWatcherContainer& container = watchers_.emplace(watcher.Bind());
  container.Start(current_);
}

void SyncWatcherSet::Notify(SyncStateContainer sync_state) {
  if (current_ == sync_state) {
    // Skip notifying if nothing has changed.
    return;
  }
  current_ = sync_state;
  for (SyncWatcherContainer& watcher : watchers_) {
    watcher.Notify(current_);
  }
}

}  // namespace ledger
