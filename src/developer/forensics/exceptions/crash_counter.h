// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_COUNTER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_COUNTER_H_

#include <lib/inspect/cpp/vmo/types.h>

#include <map>
#include <string>

namespace forensics {
namespace exceptions {

// Logs the crash count per-moniker in Inspect.
class CrashCounter {
 public:
  explicit CrashCounter(inspect::Node* root_node);
  void Increment(const std::string& moniker);

 private:
  inspect::Node crash_counts_node_;
  std::map<std::string, inspect::UintProperty> crash_counts_;
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_COUNTER_H_
