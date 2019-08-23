// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/inspected_head.h"

#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include "src/lib/fxl/logging.h"

namespace ledger {

InspectedHead::InspectedHead(inspect_deprecated::Node node)
    : node_(std::move(node)), outstanding_detachers_(0) {}

InspectedHead::~InspectedHead() = default;

void InspectedHead::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

fit::closure InspectedHead::CreateDetacher() {
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
