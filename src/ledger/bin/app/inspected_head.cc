// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/inspected_head.h"

#include <lib/fit/function.h>

#include "src/ledger/lib/logging/logging.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

InspectedHead::InspectedHead(inspect_deprecated::Node node)
    : node_(std::move(node)), outstanding_detachers_(0) {}

InspectedHead::~InspectedHead() = default;

void InspectedHead::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool InspectedHead::IsDiscardable() const { return outstanding_detachers_ == 0; }

fit::closure InspectedHead::CreateDetacher() {
  outstanding_detachers_++;
  return [this]() {
    LEDGER_DCHECK(outstanding_detachers_ > 0);
    outstanding_detachers_--;
    if (IsDiscardable() && on_discardable_) {
      on_discardable_();
    }
  };
}

}  // namespace ledger
