// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_ENCODING_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_ENCODING_H_

#include <memory>
#include <vector>

#include <rapidjson/document.h>

#include "peridot/bin/cloud_provider_firebase/page_handler/public/commit.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/public/record.h"

namespace cloud_provider_firebase {

// These methods encode and decode commits specifically for storing in Firebase
// Realtime Database.

// Encodes a batch of commits as a JSON dictionary suitable for storing in
// Firebase Realtime Database.
//
// For each commit, in addition to the commit content, a timestamp placeholder
// is added, making Firebase tag the commit with a server timestamp.
bool EncodeCommits(const std::vector<Commit>& commits,
                   std::string* output_json);

// Decodes multiple commits from the JSON representation of an object holding
// them in Firebase Realtime Database. If successful, the method returns true,
// and |output_records| contains the decoded commits along with their
// timestamps.
bool DecodeMultipleCommits(const std::string& json,
                           std::vector<Record>* output_records);

bool DecodeCommitFromValue(const rapidjson::Value& value,
                           std::unique_ptr<Record>* output_record);

bool DecodeMultipleCommitsFromValue(const rapidjson::Value& value,
                                    std::vector<Record>* output_records);

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_ENCODING_H_
