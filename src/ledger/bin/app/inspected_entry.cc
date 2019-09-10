// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/inspected_entry.h"

#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <vector>

#include "src/ledger/bin/inspect/inspect.h"
#include "src/lib/fxl/logging.h"

namespace ledger {

InspectedEntry::InspectedEntry(inspect_deprecated::Node node, std::vector<uint8_t> value)
    : node_(std::move(node)),
      value_(
          node_.CreateByteVectorProperty(kValueInspectPathComponent.ToString(), std::move(value))),
      outstanding_detachers_(0) {}

InspectedEntry::~InspectedEntry() = default;

void InspectedEntry::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

fit::closure InspectedEntry::CreateDetacher() {
  outstanding_detachers_++;
  return [this]() {
    FXL_DCHECK(outstanding_detachers_ > 0);
    outstanding_detachers_--;
    if (on_empty_callback_ && outstanding_detachers_ == 0) {
      on_empty_callback_();
    }
  };
}

}  // namespace ledger
