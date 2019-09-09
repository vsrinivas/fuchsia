// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTED_COMMIT_H_
#define SRC_LEDGER_BIN_APP_INSPECTED_COMMIT_H_

#include <lib/callback/auto_cleanable.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <memory>
#include <vector>

#include "src/ledger/bin/app/inspectable_page.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {

// Represents to Inspect a commit.
class InspectedCommit final {
 public:
  explicit InspectedCommit(inspect_deprecated::Node node,
                           std::unique_ptr<const storage::Commit> commit);
  ~InspectedCommit();

  void set_on_empty(fit::closure on_empty_callback);

  fit::closure CreateDetacher();

 private:
  void CheckEmpty();

  inspect_deprecated::Node node_;
  inspect_deprecated::Node parents_node_;
  std::vector<inspect_deprecated::Node> parents_;
  fit::closure on_empty_callback_;
  int64_t outstanding_detachers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectedCommit);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTED_COMMIT_H_
