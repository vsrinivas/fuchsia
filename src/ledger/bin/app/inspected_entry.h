// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTED_ENTRY_H_
#define SRC_LEDGER_BIN_APP_INSPECTED_ENTRY_H_

#include <lib/fit/function.h>

#include <vector>

#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

// Represents to Inspect an entry.
class InspectedEntry final {
 public:
  explicit InspectedEntry(inspect_deprecated::Node node, std::vector<uint8_t> value);
  InspectedEntry(const InspectedEntry&) = delete;
  InspectedEntry& operator=(const InspectedEntry&) = delete;
  ~InspectedEntry();

  void SetOnDiscardable(fit::closure on_discardable);

  bool IsDiscardable() const;

  fit::closure CreateDetacher();

 private:
  inspect_deprecated::Node node_;
  inspect_deprecated::ByteVectorProperty value_;
  fit::closure on_discardable_;
  // TODO(nathaniel): This integer should be replaced with a (weak-pointer-less in this case)
  // TokenManager.
  int64_t outstanding_detachers_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTED_ENTRY_H_
