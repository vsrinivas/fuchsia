// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTED_ENTRY_H_
#define SRC_LEDGER_BIN_APP_INSPECTED_ENTRY_H_

#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <vector>

#include "src/lib/fxl/macros.h"

namespace ledger {

// Represents to Inspect an entry.
class InspectedEntry final {
 public:
  explicit InspectedEntry(inspect_deprecated::Node node, std::vector<uint8_t> value);
  ~InspectedEntry();

  void set_on_empty(fit::closure on_empty_callback);

  fit::closure CreateDetacher();

 private:
  inspect_deprecated::Node node_;
  inspect_deprecated::ByteVectorProperty value_;
  fit::closure on_empty_callback_;
  // TODO(nathaniel): This integer should be replaced with a (weak-pointer-less in this case)
  // TokenManager.
  int64_t outstanding_detachers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectedEntry);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTED_ENTRY_H_
