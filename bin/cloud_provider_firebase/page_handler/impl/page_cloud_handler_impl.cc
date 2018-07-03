// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/page_cloud_handler_impl.h"

#include <lib/callback/trace_callback.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/encoding.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/impl/timestamp_conversions.h"
#include "peridot/lib/firebase/encoding.h"
#include "peridot/lib/firebase/status.h"

namespace cloud_provider_firebase {
namespace {
// The root path under which all commits are stored.
constexpr fxl::StringView kCommitRoot = "commits";
}  // namespace

PageCloudHandlerImpl::PageCloudHandlerImpl(firebase::Firebase* firebase,
                                           gcs::CloudStorage* cloud_storage)
    : firebase_(firebase), cloud_storage_(cloud_storage) {}

PageCloudHandlerImpl::~PageCloudHandlerImpl() {}

void PageCloudHandlerImpl::AddCommits(const std::string& auth_token,
                                      std::vector<Commit> commits,
                                      fit::function<void(Status)> callback) {
  auto traced_callback = TRACE_CALLBACK(
      std::move(callback), "cloud_provider_firebase", "add_commits");

  std::string encoded_batch;
  bool ok = EncodeCommits(commits, &encoded_batch);
  FXL_DCHECK(ok);

  firebase_->Patch(
      kCommitRoot.ToString(), GetQueryParams(auth_token, ""), encoded_batch,
      [callback = std::move(traced_callback)](firebase::Status status) {
        callback(ConvertFirebaseStatus(status));
      });
}

void PageCloudHandlerImpl::WatchCommits(const std::string& auth_token,
                                        const std::string& min_timestamp,
                                        CommitWatcher* watcher) {
  watchers_[watcher] = std::make_unique<WatchClientImpl>(
      firebase_, kCommitRoot.ToString(),
      GetQueryParams(auth_token, min_timestamp), watcher);
}

void PageCloudHandlerImpl::UnwatchCommits(CommitWatcher* watcher) {
  watchers_.erase(watcher);
}

void PageCloudHandlerImpl::GetCommits(
    const std::string& auth_token, const std::string& min_timestamp,
    fit::function<void(Status, std::vector<Record>)> callback) {
  auto traced_callback = TRACE_CALLBACK(
      std::move(callback), "cloud_provider_firebase", "get_commits");

  firebase_->Get(
      kCommitRoot.ToString(), GetQueryParams(auth_token, min_timestamp),
      [callback = std::move(traced_callback)](
          firebase::Status status, std::unique_ptr<rapidjson::Value> value) {
        if (status != firebase::Status::OK) {
          callback(ConvertFirebaseStatus(status), std::vector<Record>());
          return;
        }
        if (value->IsNull()) {
          // No commits synced for this page yet.
          callback(Status::OK, std::vector<Record>());
          return;
        }
        if (!value->IsObject()) {
          callback(Status::PARSE_ERROR, std::vector<Record>());
          return;
        }
        std::vector<Record> records;
        if (!DecodeMultipleCommitsFromValue(*value, &records)) {
          callback(Status::PARSE_ERROR, std::vector<Record>());
          return;
        }
        callback(Status::OK, std::move(records));
      });
}

void PageCloudHandlerImpl::AddObject(const std::string& auth_token,
                                     ObjectDigestView object_digest,
                                     fsl::SizedVmo data,
                                     fit::function<void(Status)> callback) {
  auto traced_callback = TRACE_CALLBACK(
      std::move(callback), "cloud_provider_firebase", "add_objects");

  // Even though this yields path to be used in GCS, we use Firebase key
  // encoding, as it happens to produce valid GCS object names. To be revisited
  // when we redo the encoding in LE-118.
  cloud_storage_->UploadObject(
      auth_token, firebase::EncodeKey(object_digest), std::move(data),
      [callback = std::move(traced_callback)](gcs::Status status) {
        callback(ConvertGcsStatus(status));
      });
}

void PageCloudHandlerImpl::GetObject(
    const std::string& auth_token, ObjectDigestView object_digest,
    fit::function<void(Status status, uint64_t size, zx::socket data)>
        callback) {
  auto traced_callback = TRACE_CALLBACK(
      std::move(callback), "cloud_provider_firebase", "get_object");

  cloud_storage_->DownloadObject(
      auth_token, firebase::EncodeKey(object_digest),
      [callback = std::move(traced_callback)](gcs::Status status, uint64_t size,
                                              zx::socket data) {
        callback(ConvertGcsStatus(status), size, std::move(data));
      });
}

std::vector<std::string> PageCloudHandlerImpl::GetQueryParams(
    const std::string& auth_token, const std::string& min_timestamp) {
  std::vector<std::string> result;

  if (!auth_token.empty()) {
    result.push_back("auth=" + auth_token);
  }

  if (!min_timestamp.empty()) {
    result.emplace_back("orderBy=\"timestamp\"");
    result.push_back("startAt=" + fxl::NumberToString(
                                      BytesToServerTimestamp(min_timestamp)));
  }

  return result;
}

}  // namespace cloud_provider_firebase
