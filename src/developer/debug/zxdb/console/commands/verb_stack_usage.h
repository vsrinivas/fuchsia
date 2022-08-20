// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STACK_USAGE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STACK_USAGE_H_

#include <string>
#include <vector>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/common/err_or.h"

namespace zxdb {

class ConsoleContext;
class Thread;
struct VerbRecord;

VerbRecord GetStackUsageVerbRecord();

// Information for one stack (either the safe or unsafe stack).
struct OneStackUsage {
  // Size of the VMO reserved for the stack.
  uint64_t total = 0;

  // Current stack bytes actually in use.
  uint64_t used = 0;

  // Stack bytes in committed memory.
  uint64_t committed = 0;

  // Number of bytes in whole pages between the current top of the stack and the committed pages
  // (these bytes could theoretically be thrown away).
  uint64_t wasted = 0;

  // Used for accumulating totals.
  OneStackUsage& operator+=(const OneStackUsage& other) {
    total += other.total;
    used += other.used;
    committed += other.committed;
    wasted += other.wasted;
    return *this;
  }
};

struct ThreadStackUsage {
  int id = 0;
  std::string name;

  ErrOr<OneStackUsage> safe_stack = Err("No safe stack info.");
  ErrOr<OneStackUsage> unsafe_stack = Err("No unsafe stack info.");
};

ThreadStackUsage GetThreadStackUsage(ConsoleContext* console_context,
                                     const std::vector<debug_ipc::AddressRegion>& map,
                                     Thread* thread, uint64_t unsafe_stack_pointer);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STACK_USAGE_H_
