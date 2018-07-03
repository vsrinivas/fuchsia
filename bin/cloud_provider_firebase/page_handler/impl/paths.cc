// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/paths.h"

#include <string>

#include <lib/fxl/strings/concatenate.h>

#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/lib/firebase/encoding.h"

namespace cloud_provider_firebase {

namespace {
constexpr char kGcsSeparator[] = "%2F";
constexpr char kFirebaseSeparator[] = "/";
constexpr char kDefaultCloudPrefix[] = "__default__";
}  // namespace

// Even though this yields a path to be used in GCS, we use Firebase key
// encoding, as it happens to produce valid GCS object names. To be revisited
// when we redo the encoding in LE-118.
std::string GetGcsPrefixForApp(fxl::StringView user_id,
                               fxl::StringView app_id) {
  // TODO(ppi): remove the fallback to encoded user id once we drop support for
  // non authenticated sync.
  std::string encoded_user_id = firebase::CanKeyBeVerbatim(user_id)
                                    ? user_id.ToString()
                                    : firebase::EncodeKey(user_id);
  return fxl::Concatenate({firebase::EncodeKey(kDefaultCloudPrefix),
                           kGcsSeparator, encoded_user_id, kGcsSeparator,
                           storage::kSerializationVersion, kGcsSeparator,
                           firebase::EncodeKey(app_id)});
}

std::string GetGcsPrefixForPage(fxl::StringView app_path,
                                fxl::StringView page_id) {
  return fxl::Concatenate(
      {app_path, kGcsSeparator, firebase::EncodeKey(page_id), kGcsSeparator});
}

std::string GetFirebasePathForUser(fxl::StringView user_id) {
  // TODO(ppi): remove the fallback to encoded user id once we drop support for
  // non authenticated sync.
  std::string encoded_user_id = firebase::CanKeyBeVerbatim(user_id)
                                    ? user_id.ToString()
                                    : firebase::EncodeKey(user_id);
  return fxl::Concatenate({firebase::EncodeKey(kDefaultCloudPrefix),
                           kFirebaseSeparator, encoded_user_id,
                           kFirebaseSeparator, storage::kSerializationVersion});
}

std::string GetFirebasePathForApp(fxl::StringView user_id,
                                  fxl::StringView app_id) {
  std::string user_path = GetFirebasePathForUser(user_id);
  return fxl::Concatenate(
      {user_path, kFirebaseSeparator, firebase::EncodeKey(app_id)});
}

std::string GetFirebasePathForPage(fxl::StringView app_path,
                                   fxl::StringView page_id) {
  return fxl::Concatenate(
      {app_path, kFirebaseSeparator, firebase::EncodeKey(page_id)});
}

}  // namespace cloud_provider_firebase
