// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/inspected_entry.h"

#include <lib/fit/function.h>

#include <vector>

#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

InspectedEntry::InspectedEntry(inspect_deprecated::Node node, std::vector<uint8_t> value)
    : node_(std::move(node)),
      value_(node_.CreateByteVectorProperty(convert::ToString(kValueInspectPathComponent),
                                            std::move(value))),
      outstanding_detachers_(0) {}

InspectedEntry::~InspectedEntry() = default;

void InspectedEntry::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool InspectedEntry::IsDiscardable() const { return outstanding_detachers_ == 0; }

fit::closure InspectedEntry::CreateDetacher() {
  outstanding_detachers_++;
  return [this]() {
    LEDGER_DCHECK(outstanding_detachers_ > 0);
    outstanding_detachers_--;
    if (on_discardable_ && IsDiscardable()) {
      on_discardable_();
    }
  };
}

}  // namespace ledger
