// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/paths.h"

#include <string>

#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/ftl/strings/concatenate.h"

namespace cloud_provider {

namespace {
constexpr char kGcsSeparator[] = "%2F";
constexpr char kFirebaseSeparator[] = "/";
constexpr char kDefaultCloudPrefix[] = "__default__";
}  // namespace

// Even though this yields a path to be used in GCS, we use Firebase key
// encoding, as it happens to produce valid GCS object names. To be revisited
// when we redo the encoding in LE-118.
std::string GetGcsPrefixForApp(ftl::StringView user_id,
                               ftl::StringView app_id) {
  // TODO(ppi): remove the fallback to encoded user id once we drop support for
  // non authenticated sync.
  std::string encoded_user_id = firebase::CanKeyBeVerbatim(user_id)
                                    ? user_id.ToString()
                                    : firebase::EncodeKey(user_id);
  return ftl::Concatenate({firebase::EncodeKey(kDefaultCloudPrefix),
                           kGcsSeparator, encoded_user_id, kGcsSeparator,
                           storage::kSerializationVersion, kGcsSeparator,
                           firebase::EncodeKey(app_id)});
}

std::string GetGcsPrefixForPage(ftl::StringView app_path,
                                ftl::StringView page_id) {
  return ftl::Concatenate(
      {app_path, kGcsSeparator, firebase::EncodeKey(page_id), kGcsSeparator});
}

std::string GetFirebasePathForUser(ftl::StringView user_id) {
  // TODO(ppi): remove the fallback to encoded user id once we drop support for
  // non authenticated sync.
  std::string encoded_user_id = firebase::CanKeyBeVerbatim(user_id)
                                    ? user_id.ToString()
                                    : firebase::EncodeKey(user_id);
  return ftl::Concatenate({firebase::EncodeKey(kDefaultCloudPrefix),
                           kFirebaseSeparator, encoded_user_id,
                           kFirebaseSeparator, storage::kSerializationVersion});
}

std::string GetFirebasePathForApp(ftl::StringView user_id,
                                  ftl::StringView app_id) {
  std::string user_path = GetFirebasePathForUser(user_id);
  return ftl::Concatenate(
      {user_path, kFirebaseSeparator, firebase::EncodeKey(app_id)});
}

std::string GetFirebasePathForPage(ftl::StringView app_path,
                                   ftl::StringView page_id) {
  return ftl::Concatenate(
      {app_path, kFirebaseSeparator, firebase::EncodeKey(page_id)});
}

}  // namespace cloud_provider
