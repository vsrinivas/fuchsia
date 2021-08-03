// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "table_holder.h"

TableHolderBase::TableHolderBase(TableHolderBase&& to_move) noexcept
    : table_set_(to_move.table_set_) {
  table_set_.TrackTableHolder(this);
}

TableHolderBase::TableHolderBase(TableSet& table_set) : table_set_(table_set) {
  table_set_.TrackTableHolder(this);
}

TableHolderBase::~TableHolderBase() { table_set_.UntrackTableHolder(this); }

void TableHolderBase::CountChurn() { table_set_.CountChurn(); }

fidl::AnyArena& TableHolderBase::allocator() { return table_set_.allocator(); }
