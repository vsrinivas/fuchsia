// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "table_set.h"

#include <zircon/compiler.h>

#include <ddk/trace/event.h>

#include "macros.h"
#include "table_holder.h"

namespace {

constexpr uint32_t kChurnCountThreshold = 256;

}  // namespace

TableSet::TableSet() : allocator_(std::make_unique<FidlAllocator>()) {}

fidl::AnyAllocator& TableSet::allocator() {
  CountChurn();
  return *allocator_;
}

void TableSet::MitigateChurn() {
  if (churn_count_ < kChurnCountThreshold) {
    return;
  }
  auto old_allocator = std::move(allocator_);
  allocator_ = std::make_unique<FidlAllocator>();
  {
    TRACE_DURATION("gfx", "TableSet::MitigateChurn() clone_to_new_allocator() loop");
    for (auto table_holder_base : tables_) {
      table_holder_base->clone_to_new_allocator();
    }
  }
  // Set this back to 0 last, so any churn_count_ increments during main part of MitigateChurn()
  // don't get counted, since that could lead to degenerate behavior with a large enough working
  // set.
  churn_count_ = 0;
  // ~old_allocator
}

void TableSet::CountChurn() { ++churn_count_; }

void TableSet::TrackTableHolder(TableHolderBase* table_holder) {
  CountChurn();
  tables_.insert(table_holder);
}

void TableSet::UntrackTableHolder(TableHolderBase* table_holder) {
  CountChurn();
  tables_.erase(table_holder);
}
