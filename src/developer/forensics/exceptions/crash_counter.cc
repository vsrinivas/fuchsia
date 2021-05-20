// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/crash_counter.h"

namespace forensics {
namespace exceptions {

CrashCounter::CrashCounter(inspect::Node* root_node)
    : crash_counts_node_(root_node->CreateChild("crash_counts")) {}

void CrashCounter::Increment(const std::string& moniker) {
  if (crash_counts_.find(moniker) == crash_counts_.end()) {
    crash_counts_.emplace(moniker, crash_counts_node_.CreateUint(moniker, 0));
  }

  crash_counts_.at(moniker).Add(1);
}

}  // namespace exceptions
}  // namespace forensics
