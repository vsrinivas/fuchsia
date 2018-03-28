// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"

#include "peridot/lib/base64url/base64url.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {
constexpr char kCommitsKey[] = "commits";
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

bool EncodeCommitBatch(const fidl::VectorPtr<cloud_provider::Commit>& commits,
                       google::firestore::v1beta1::Document* document) {
  FXL_DCHECK(document);
  // TODO(ppi): fail and return false if the resulting batch exceeds max
  // Firestore document size.
  google::firestore::v1beta1::Document result;
  google::firestore::v1beta1::ArrayValue* commit_array =
      (*result.mutable_fields())[kCommitsKey].mutable_array_value();
  for (const auto& commit : *commits) {
    google::firestore::v1beta1::MapValue* commit_value =
        commit_array->add_values()->mutable_map_value();
    *((*commit_value->mutable_fields())[kIdKey].mutable_bytes_value()) =
        convert::ToString(commit.id);
    *((*commit_value->mutable_fields())[kDataKey].mutable_bytes_value()) =
        convert::ToString(commit.data);
  }

  document->Swap(&result);
  return true;
}

bool DecodeCommitBatch(const google::firestore::v1beta1::Document& document,
                       fidl::VectorPtr<cloud_provider::Commit>* commits,
                       std::string* timestamp) {
  FXL_DCHECK(commits);
  FXL_DCHECK(timestamp);

  fidl::VectorPtr<cloud_provider::Commit> result;
  if (document.fields().count(kCommitsKey) != 1) {
    return false;
  }

  const google::firestore::v1beta1::Value& commits_value =
      document.fields().at(kCommitsKey);
  if (!commits_value.has_array_value()) {
    return false;
  }

  const google::firestore::v1beta1::ArrayValue& commits_array_value =
      commits_value.array_value();
  for (const auto& commit_value : commits_array_value.values()) {
    if (!commit_value.has_map_value()) {
      return false;
    }

    const google::firestore::v1beta1::MapValue& commit_map_value =
        commit_value.map_value();
    cloud_provider::Commit commit;
    if (commit_map_value.fields().count(kIdKey) != 1) {
      return false;
    }
    commit.id =
        convert::ToArray(commit_map_value.fields().at(kIdKey).bytes_value());
    if (commit_map_value.fields().count(kDataKey) != 1) {
      return false;
    }
    commit.data =
        convert::ToArray(commit_map_value.fields().at(kDataKey).bytes_value());
    result.push_back(std::move(commit));
  }

  // TODO(ppi): Read the server-assigned timestamp field.
  commits->swap(result);
  return true;
}

}  // namespace cloud_provider_firestore
