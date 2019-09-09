// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/inspected_commit.h"

#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <memory>
#include <utility>
#include <vector>

#include "src/ledger/bin/app/inspectable_page.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

InspectedCommit::InspectedCommit(inspect_deprecated::Node node,
                                 std::unique_ptr<const storage::Commit> commit)
    : node_(std::move(node)),
      parents_node_(node_.CreateChild(kParentsInspectPathComponent.ToString())),
      outstanding_detachers_(0) {
  for (const storage::CommitIdView& parent_id : commit->GetParentIds()) {
    parents_.emplace_back(parents_node_.CreateChild(CommitIdToDisplayName(parent_id.ToString())));
  }
}

InspectedCommit::~InspectedCommit() = default;

void InspectedCommit::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

fit::closure InspectedCommit::CreateDetacher() {
  outstanding_detachers_++;
  return [this] {
    outstanding_detachers_--;
    CheckEmpty();
  };
}

void InspectedCommit::CheckEmpty() {
  if (on_empty_callback_ && outstanding_detachers_ == 0) {
    on_empty_callback_();
  }
}

}  // namespace ledger
