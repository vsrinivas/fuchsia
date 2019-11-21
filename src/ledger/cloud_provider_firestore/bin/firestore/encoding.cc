// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/firestore/encoding.h"

#include "peridot/lib/base64url/base64url.h"
#include "src/ledger/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {
constexpr char kCommitsKey[] = "commits";
constexpr char kTimestampKey[] = "timestamp";
constexpr char kIdKey[] = "id";
constexpr char kDataKey[] = "data";
}  // namespace

std::string EncodeKey(fxl::StringView input) {
  std::string encoded = base64url::Base64UrlEncode(input);
  encoded.append(1u, '+');
  return encoded;
}

bool DecodeKey(fxl::StringView input, std::string* output) {
  if (input.empty() || input.back() != '+') {
    return false;
  }

  input.remove_suffix(1u);
  return base64url::Base64UrlDecode(input, output);
}

bool EncodeCommitBatch(const cloud_provider::CommitPack& commits,
                       google::firestore::v1beta1::Document* document) {
  FXL_DCHECK(document);

  std::vector<cloud_provider::CommitPackEntry> entries;
  if (!cloud_provider::DecodeCommitPack(commits, &entries)) {
    return false;
  }

  // TODO(ppi): fail and return false if the resulting batch exceeds max
  // Firestore document size.
  google::firestore::v1beta1::Document result;
  google::firestore::v1beta1::ArrayValue* commit_array =
      (*result.mutable_fields())[kCommitsKey].mutable_array_value();
  for (const auto& entry : entries) {
    google::firestore::v1beta1::MapValue* commit_value =
        commit_array->add_values()->mutable_map_value();
    *((*commit_value->mutable_fields())[kIdKey].mutable_bytes_value()) =
        convert::ToString(entry.id);
    *((*commit_value->mutable_fields())[kDataKey].mutable_bytes_value()) =
        convert::ToString(entry.data);
  }

  document->Swap(&result);
  return true;
}

bool DecodeCommitBatch(const google::firestore::v1beta1::Document& document,
                       std::vector<cloud_provider::CommitPackEntry>* commit_entries,
                       std::string* timestamp) {
  FXL_DCHECK(commit_entries);
  FXL_DCHECK(timestamp);

  std::vector<cloud_provider::CommitPackEntry> result;
  if (document.fields().count(kCommitsKey) != 1) {
    return false;
  }

  const google::firestore::v1beta1::Value& commits_value = document.fields().at(kCommitsKey);
  if (!commits_value.has_array_value()) {
    return false;
  }

  const google::firestore::v1beta1::ArrayValue& commits_array_value = commits_value.array_value();
  for (const auto& commit_value : commits_array_value.values()) {
    if (!commit_value.has_map_value()) {
      return false;
    }

    const google::firestore::v1beta1::MapValue& commit_map_value = commit_value.map_value();
    cloud_provider::CommitPackEntry entry;
    if (commit_map_value.fields().count(kIdKey) != 1) {
      return false;
    }
    entry.id = convert::ToString(commit_map_value.fields().at(kIdKey).bytes_value());
    if (commit_map_value.fields().count(kDataKey) != 1) {
      return false;
    }
    entry.data = convert::ToString(commit_map_value.fields().at(kDataKey).bytes_value());
    result.push_back(std::move(entry));
  }

  // Read the timestamp field.
  if (document.fields().count(kTimestampKey) == 1) {
    const google::firestore::v1beta1::Value& timestamp_value = document.fields().at(kTimestampKey);
    if (!timestamp_value.has_timestamp_value()) {
      return false;
    }

    if (!timestamp_value.timestamp_value().SerializeToString(timestamp)) {
      return false;
    }
  } else if (document.fields().count(kTimestampKey) != 0) {
    // The timestamp field should appear only 0 or 1 time.
    return false;
  }

  commit_entries->swap(result);
  return true;
}

}  // namespace cloud_provider_firestore
