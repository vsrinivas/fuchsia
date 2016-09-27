// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/btree/position.h"

namespace storage {

Position::Position(std::unique_ptr<const TreeNode> node,
                   int entry_index,
                   int child_index)
    : node(std::move(node)),
      entry_index(entry_index),
      child_index(child_index) {}

Position::~Position() {}

}  // namespace storage
