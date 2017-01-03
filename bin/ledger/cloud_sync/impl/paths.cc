// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/paths.h"

#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/ftl/strings/concatenate.h"

namespace cloud_sync {

namespace {
constexpr char kGcsSeparator[] = "%2F";
constexpr char kFirebaseSeparator[] = "/";
}  // namespace

// Even though this yields a path to be used in GCS, we use Firebase key
// encoding, as it happens to produce valid GCS object names. To be revisited
// when we redo the encoding in LE-118.
std::string GetGcsPrefixForApp(ftl::StringView user_prefix,
                               ftl::StringView app_id) {
  return ftl::Concatenate({firebase::EncodeKey(user_prefix), kGcsSeparator,
                           storage::kSerializationVersion, kGcsSeparator,
                           firebase::EncodeKey(app_id)});
}

std::string GetGcsPrefixForPage(ftl::StringView app_path,
                                ftl::StringView page_id) {
  return ftl::Concatenate(
      {app_path, kGcsSeparator, firebase::EncodeKey(page_id), kGcsSeparator});
}

std::string GetFirebasePathForApp(ftl::StringView user_prefix,
                                  ftl::StringView app_id) {
  return ftl::Concatenate({firebase::EncodeKey(user_prefix), kFirebaseSeparator,
                           storage::kSerializationVersion, kFirebaseSeparator,
                           firebase::EncodeKey(app_id)});
}

std::string GetFirebasePathForPage(ftl::StringView app_path,
                                   ftl::StringView page_id) {
  return ftl::Concatenate(
      {app_path, kFirebaseSeparator, firebase::EncodeKey(page_id)});
}

}  // namespace cloud_sync
