// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_ENCODING_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_ENCODING_H_

#include <map>
#include <string>

#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace btree {

bool CheckValidTreeNodeSerialization(absl::string_view data);

// Computes and sets the entry_id of the given entry, if it is not already present.
void SetEntryIdIfMissing(Entry* entry);

std::string EncodeNode(uint8_t level, const std::vector<Entry>& entries,
                       const std::map<size_t, ObjectIdentifier>& children);

bool DecodeNode(absl::string_view data, ObjectIdentifierFactory* factory, uint8_t* level,
                std::vector<Entry>* res_entries, std::map<size_t, ObjectIdentifier>* res_children);

}  // namespace btree
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_ENCODING_H_
