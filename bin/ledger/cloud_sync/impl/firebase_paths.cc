// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/firebase_paths.h"

#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/ftl/strings/concatenate.h"

namespace cloud_sync {

std::string GetFirebasePathForPage(ftl::StringView user_prefix,
                                   ftl::StringView app_id,
                                   ftl::StringView page_id) {
  return ftl::Concatenate(
      {firebase::EncodeKey(user_prefix), "/", storage::kSerializationVersion,
       "/", firebase::EncodeKey(app_id), "/", firebase::EncodeKey(page_id)});
}

}  // namespace cloud_sync
