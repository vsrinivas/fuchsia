// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/testing/encoding.h"

#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"

namespace cloud_provider_firestore {

namespace {
// Must match |kTimestampKey| in
// peridot/bin/cloud_provider_firestore/firestore/encoding.cc.
constexpr char kTimestampKey[] = "timestamp";
}  // namespace

bool EncodeCommitBatchWithTimestamp(
    const fidl::VectorPtr<cloud_provider::Commit>& commits,
    std::string timestamp, google::firestore::v1beta1::Document* document) {
  google::firestore::v1beta1::Document result;
  if (!EncodeCommitBatch(commits, &result)) {
    return false;
  }

  google::protobuf::Timestamp protobuf_timestamp;
  if (!protobuf_timestamp.ParseFromString(timestamp)) {
    return false;
  }
  google::firestore::v1beta1::Value& timestamp_value =
      (*result.mutable_fields())[kTimestampKey];
  *(timestamp_value.mutable_timestamp_value()) = std::move(protobuf_timestamp);

  document->Swap(&result);
  return true;
}

}  // namespace cloud_provider_firestore
