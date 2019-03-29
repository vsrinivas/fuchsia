// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_ENCODING_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_ENCODING_H_

#include <string>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <google/firestore/v1beta1/document.pb.h>
#include <lib/fidl/cpp/vector.h>
#include <src/lib/fxl/strings/string_view.h>

#include "peridot/lib/commit_pack/commit_pack.h"
#include "src/ledger/cloud_provider_firestore/bin/include/types.h"

namespace cloud_provider_firestore {

// Encodes the data so that it can be used as a Firestore key.
//
// The resulting encoding is base64url with a single '+' character appended at
// the end. This is because Firestore disallows keys matching the regular
// expression `__.*__` which would otherwise be possible to produce.
//
// See https://cloud.google.com/firestore/quotas#limits.
std::string EncodeKey(fxl::StringView input);

// Decodes a Firestore key encoded using |EncodeKey|.
bool DecodeKey(fxl::StringView input, std::string* output);

// Encodes a batch of commits in the cloud provider FIDL format as a Firestore
// document.
bool EncodeCommitBatch(const cloud_provider::CommitPack& commits,
                       google::firestore::v1beta1::Document* document);

// Decodes a Firestore document representing a commit batch.
bool DecodeCommitBatch(
    const google::firestore::v1beta1::Document& document,
    std::vector<cloud_provider::CommitPackEntry>* commit_entries,
    std::string* timestamp);

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_ENCODING_H_
