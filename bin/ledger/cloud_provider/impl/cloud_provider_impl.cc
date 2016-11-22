// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/cloud_provider_impl.h"

#include "apps/ledger/src/cloud_provider/impl/encoding.h"
#include "apps/ledger/src/cloud_provider/impl/timestamp_conversions.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/status.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/vmo/strings.h"

namespace cloud_provider {
namespace {
// The root path under which all commits are stored.
constexpr ftl::StringView kCommitRoot = "commits";

// The root path under which all objects are stored.
constexpr ftl::StringView kObjectRoot = "objects";

// Returns the path under which the given commit is stored.
std::string GetCommitPath(const Commit& commit) {
  return ftl::Concatenate({kCommitRoot, "/", firebase::EncodeKey(commit.id)});
}

// Returns the path under which the given object is stored.
std::string GetObjectPath(ObjectIdView object_id) {
  return ftl::Concatenate(
      {kObjectRoot, "/", firebase::EncodeKey(object_id.ToString())});
}
}  // namespace

CloudProviderImpl::CloudProviderImpl(firebase::Firebase* firebase)
    : firebase_(firebase) {}

CloudProviderImpl::~CloudProviderImpl() {}

void CloudProviderImpl::AddCommit(const Commit& commit,
                                  const std::function<void(Status)>& callback) {
  std::string encoded_commit;
  bool ok = EncodeCommit(commit, &encoded_commit);
  FTL_DCHECK(ok);

  firebase_->Put(GetCommitPath(commit), encoded_commit,
                 [callback](firebase::Status status) {
                   if (status == firebase::Status::OK) {
                     callback(Status::OK);
                   } else {
                     callback(Status::UNKNOWN_ERROR);
                   }
                 });
}

void CloudProviderImpl::WatchCommits(const std::string& min_timestamp,
                                     CommitWatcher* watcher) {
  watchers_[watcher] = std::make_unique<WatchClientImpl>(
      firebase_, kCommitRoot.ToString(), GetTimestampQuery(min_timestamp),
      watcher);
}

void CloudProviderImpl::UnwatchCommits(CommitWatcher* watcher) {
  watchers_.erase(watcher);
}

void CloudProviderImpl::GetCommits(
    const std::string& min_timestamp,
    std::function<void(Status, std::vector<Record>)> callback) {
  firebase_->Get(
      kCommitRoot.ToString(), GetTimestampQuery(min_timestamp),
      [callback](firebase::Status status, const rapidjson::Value& value) {
        if (status != firebase::Status::OK) {
          callback(Status::UNKNOWN_ERROR, std::vector<Record>());
          return;
        }
        if (value.IsNull()) {
          // No commits synced for this page yet.
          callback(Status::OK, std::vector<Record>());
          return;
        }
        if (!value.IsObject()) {
          callback(Status::UNKNOWN_ERROR, std::vector<Record>());
          return;
        }
        std::vector<Record> records;
        if (!DecodeMultipleCommitsFromValue(value, &records)) {
          callback(Status::UNKNOWN_ERROR, std::vector<Record>());
          return;
        }
        callback(Status::OK, std::move(records));
      });
}

void CloudProviderImpl::AddObject(ObjectIdView object_id,
                                  mx::vmo data,
                                  std::function<void(Status)> callback) {
  std::string data_str;
  auto result = mtl::StringFromVmo(data, &data_str);
  if (!result) {
    callback(Status::INTERNAL_ERROR);
    return;
  }

  std::string encoded =
      ftl::Concatenate({"\"", firebase::EncodeValue(data_str), "\""});
  // Maximum size of a value stored in Firebase is 10MB.
  // TODO(ppi): switch to GCS for the object API.
  if (encoded.size() > 10'000'000) {
    FTL_LOG(ERROR) << "Failed to upload object to Firebase - object too big.";
    callback(Status::ARGUMENT_ERROR);
    return;
  }

  firebase_->Put(GetObjectPath(object_id), encoded,
                 [callback](firebase::Status status) {
                   if (status == firebase::Status::OK) {
                     callback(Status::OK);
                   } else {
                     callback(Status::NETWORK_ERROR);
                   }
                 });
}

void CloudProviderImpl::GetObject(
    ObjectIdView object_id,
    std::function<void(Status status,
                       uint64_t size,
                       mx::datapipe_consumer data)> callback) {
  firebase_->Get(
      GetObjectPath(object_id), "",
      [callback](firebase::Status status, const rapidjson::Value& value) {
        if (status == firebase::Status::OK) {
          std::string data;
          if (!value.IsString() ||
              !firebase::Decode(value.GetString(), &data)) {
            callback(Status::INTERNAL_ERROR, 0u, mx::datapipe_consumer());
            return;
          }
          callback(Status::OK, data.size(),
                   mtl::WriteStringToConsumerHandle(data));
        } else {
          callback(Status::NETWORK_ERROR, 0u, mx::datapipe_consumer());
        }
      });
}

std::string CloudProviderImpl::GetTimestampQuery(
    const std::string& min_timestamp) {
  if (min_timestamp.empty()) {
    return "";
  }

  return "orderBy=\"timestamp\"&startAt=" +
         ftl::NumberToString(BytesToServerTimestamp(min_timestamp));
}

}  // namespace cloud_provider
