// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_TESTING_ENCODING_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_TESTING_ENCODING_H_

#include <string>

#include <google/firestore/v1beta1/document.pb.h>
#include <lib/fidl/cpp/vector.h>

#include "peridot/bin/cloud_provider_firestore/include/types.h"

namespace cloud_provider_firestore {

// Encodes a batch of commits along with the givem timestamp.
//
// The resulting Document matches what the server returns from queries: the
// given timestamp appears as the server-set timestamp.
//
// |timestamp| must be a valid serialized protobuf::Timestamp.
bool EncodeCommitBatchWithTimestamp(
    const fidl::VectorPtr<cloud_provider::Commit>& commits,
    std::string timestamp, google::firestore::v1beta1::Document* document);

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_TESTING_ENCODING_H_
