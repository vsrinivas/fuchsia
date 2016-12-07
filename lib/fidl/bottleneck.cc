// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/bottleneck.h"

namespace modular {

void Bottleneck::operator()(Result done) {
  results_.emplace_back(done);
  if (results_.size() == 1) {
    Call();
  }
}

void Bottleneck::Call() {
  cover_ = results_.size();
  operation_([this] { Done(); });
}

void Bottleneck::Done() {
  if (kind_ == BACK || cover_ == results_.size()) {
    // If as result of calling result callbacks, more operation requests
    // arrive, they are part of a new round of invocation of operation_.
    std::vector<Result> results = std::move(results_);
    for (auto& done : results) {
      done();
    }
  } else {
    // If there were more requests since the first invocation, and
    // this is a FRONT bottleneck, invoke the operation again.
    Call();
  }
}

}  // namespace modular
