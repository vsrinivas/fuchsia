// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/commit_generated.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

static_assert(sizeof(IdStorage) == kCommitIdSize, "storage size for id is incorrect");

const IdStorage* ToIdStorage(CommitIdView id) {
  return reinterpret_cast<const IdStorage*>(id.data());
}

CommitIdView ToCommitIdView(const IdStorage* fb_id_storage) {
  return CommitIdView(
      absl::string_view(reinterpret_cast<const char*>(fb_id_storage), sizeof(IdStorage)));
}
}  // namespace storage
