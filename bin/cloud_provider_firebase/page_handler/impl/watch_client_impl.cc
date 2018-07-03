// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/watch_client_impl.h"

#include <lib/fxl/logging.h>
#include <lib/fxl/time/time_delta.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/encoding.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/impl/timestamp_conversions.h"

namespace cloud_provider_firebase {

WatchClientImpl::WatchClientImpl(firebase::Firebase* firebase,
                                 const std::string& firebase_key,
                                 const std::vector<std::string>& query_params,
                                 CommitWatcher* commit_watcher)
    : firebase_(firebase), commit_watcher_(commit_watcher) {
  firebase_->Watch(firebase_key, query_params, this);
}

WatchClientImpl::~WatchClientImpl() {
  if (!errored_) {
    firebase_->UnWatch(this);
  }
}

void WatchClientImpl::OnPut(const std::string& path,
                            const rapidjson::Value& value) {
  Handle(path, value);
}

void WatchClientImpl::OnPatch(const std::string& path,
                              const rapidjson::Value& value) {
  Handle(path, value);
}

void WatchClientImpl::Handle(const std::string& path,
                             const rapidjson::Value& value) {
  if (errored_) {
    return;
  }

  if (path == "/" && value.IsNull()) {
    // If there are no matching commits, the first response after setting up the
    // watcher is null. Don't panic.
    return;
  }

  if (!value.IsObject()) {
    HandleDecodingError(path, value, "received data is not a dictionary");
    return;
  }

  if (path == "/") {
    // The initial put event contains multiple commits.
    std::vector<Record> records;
    if (!DecodeMultipleCommitsFromValue(value, &records)) {
      HandleDecodingError(path, value,
                          "failed to decode a collection of commits");
      return;
    }
    for (auto& record : records) {
      ProcessRecord(std::move(record));
    }
    return;
  }

  if (path.empty() || path.front() != '/') {
    HandleDecodingError(path, value, "invalid path");
    return;
  }

  std::unique_ptr<Record> record;
  if (!DecodeCommitFromValue(value, &record)) {
    HandleDecodingError(path, value, "failed to decode the commit");
    return;
  }

  ProcessRecord(std::move(*record));
}

void WatchClientImpl::ProcessRecord(Record record) {
  if (!batch_timestamp_.empty()) {
    // There is a pending batch already, verify that the new commits is part of
    // it.
    if (record.timestamp != batch_timestamp_) {
      FXL_LOG(ERROR) << "Two batches of commits are intermixed. "
                     << "This should not have happened, please file a bug.";
      HandleError();
      return;
    }

    // Received record is for the current batch.
    if (record.batch_size != batch_size_) {
      FXL_LOG(ERROR) << "The size of the commit batch is inconsistent. "
                     << "This should not have happened, please file a bug.";
      HandleError();
      return;
    }
  } else {
    // There is no pending batch, start a new one.
    FXL_DCHECK(batch_.empty());
    batch_timestamp_ = record.timestamp;
    batch_size_ = record.batch_size;
    batch_.reserve(batch_size_);
  }

  // Add the new commit to the batch.
  batch_.push_back(std::move(record));

  // If the batch is complete, commit.
  if (batch_.size() == batch_size_) {
    CommitBatch();
  }
}

void WatchClientImpl::CommitBatch() {
  FXL_DCHECK(batch_.size() == batch_size_);
  std::sort(batch_.begin(), batch_.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.batch_position < rhs.batch_position;
  });
  commit_watcher_->OnRemoteCommits(std::move(batch_));
  batch_.clear();
  batch_timestamp_.clear();
  batch_size_ = 0;
}

void WatchClientImpl::OnCancel() {
  FXL_LOG(ERROR) << "Firebase cancelled the watch request.";
  HandleError();
  commit_watcher_->OnConnectionError();
}

void WatchClientImpl::OnAuthRevoked(const std::string& reason) {
  FXL_LOG(INFO) << "Remote watcher needs a new token: " << reason;
  HandleError();
  commit_watcher_->OnTokenExpired();
}

void WatchClientImpl::OnMalformedEvent() {
  // Firebase already prints out debug info before calling here.
  HandleError();
  commit_watcher_->OnMalformedNotification();
}

void WatchClientImpl::OnConnectionError() {
  // Firebase already prints out debug info before calling here.
  HandleError();
  commit_watcher_->OnConnectionError();
}

void WatchClientImpl::HandleDecodingError(const std::string& path,
                                          const rapidjson::Value& value,
                                          const char error_description[]) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);

  FXL_LOG(ERROR) << "Error processing received commits: " << error_description;
  FXL_LOG(ERROR) << "Path: " << path;
  FXL_LOG(ERROR) << "Content: " << buffer.GetString();

  HandleError();
  commit_watcher_->OnMalformedNotification();
}

void WatchClientImpl::HandleError() {
  FXL_DCHECK(!errored_);
  errored_ = true;
  firebase_->UnWatch(this);
}

}  // namespace cloud_provider_firebase
