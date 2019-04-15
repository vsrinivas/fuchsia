// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_ENCODING_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_ENCODING_H_

#include <map>
#include <string>

#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {
namespace btree {

bool CheckValidTreeNodeSerialization(fxl::StringView data);

std::string EncodeNode(uint8_t level, const std::vector<Entry>& entries,
                       const std::map<size_t, ObjectIdentifier>& children);

bool DecodeNode(fxl::StringView data, uint8_t* level,
                std::vector<Entry>* res_entries,
                std::map<size_t, ObjectIdentifier>* res_children);

}  // namespace btree
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_ENCODING_H_
