// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/impl/btree/encoding.h"
#include "src/ledger/bin/storage/public/types.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  std::string bytes(reinterpret_cast<const char *>(Data), Size);

  if (storage::btree::CheckValidTreeNodeSerialization(bytes)) {
    uint8_t res_level;
    std::vector<storage::Entry> res_entries;
    std::map<size_t, storage::ObjectIdentifier> res_children;
    storage::btree::DecodeNode(bytes, &res_level, &res_entries, &res_children);
  }
  return 0;
}
