// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_TESTING_ENCODING_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_TESTING_ENCODING_H_

#include <google/firestore/v1beta1/document.pb.h>
#include <lib/fidl/cpp/vector.h>

#include <string>

#include "src/ledger/cloud_provider_firestore/bin/include/types.h"

namespace cloud_provider_firestore {

// Encodes a batch of commits along with the givem timestamp.
//
// The resulting Document matches what the server returns from queries: the
// given timestamp appears as the server-set timestamp.
//
// |timestamp| must be a valid serialized protobuf::Timestamp.
bool EncodeCommitBatchWithTimestamp(
    const cloud_provider::CommitPack& commits, std::string timestamp,
    google::firestore::v1beta1::Document* document);

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_TESTING_ENCODING_H_
