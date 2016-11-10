// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_ENCODING_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_ENCODING_H_

#include <memory>
#include <vector>

#include "apps/ledger/src/cloud_provider/public/commit.h"
#include "apps/ledger/src/cloud_provider/public/record.h"

#include <rapidjson/document.h>

namespace cloud_provider {

// These methods encode and decode commits specifically for storing
// in Firebase Realtime Database.

// Encodes a commit as a JSON string suitable for storing in
// Firebase Realtime Database. In addition to the commit content, a
// timestamp placeholder is added, making Firebase tag the commit with a
// server timestamp.
bool EncodeCommit(const Commit& commit, std::string* output_json);

// Decodes a commit from the JSON representation in Firebase
// Realtime Database. If successful, the method returns true, and
// |output_record| contains the decoded commit, along with opaque
// server timestamp.
bool DecodeCommit(const std::string& json,
                  std::unique_ptr<Record>* output_record);

// Decodes multiple commits from the JSON representation of an
// object holding them in Firebase Realtime Database. If successful, the method
// returns true, and |output_records| contain the decoded commits
// along with their timestamps.
bool DecodeMultipleCommits(const std::string& json,
                           std::vector<Record>* output_records);

bool DecodeCommitFromValue(const rapidjson::Value& value,
                           std::unique_ptr<Record>* output_record);

bool DecodeMultipleCommitsFromValue(const rapidjson::Value& value,
                                    std::vector<Record>* output_records);

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_ENCODING_H_
