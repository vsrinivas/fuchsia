// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STACK_USAGE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STACK_USAGE_H_

#include <string>
#include <vector>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

class ConsoleContext;
class Thread;
struct VerbRecord;

VerbRecord GetStackUsageVerbRecord();

struct ThreadStackUsage {
  // The ID and name are always filled in.
  int id = 0;
  std::string name;

  // Set if there was an error getting any statistics. If set, the values below are invalid.
  Err err;

  // Size of the VMO reserved for the stack.
  uint64_t total = 0;

  // Current stack bytes actually in use.
  uint64_t used = 0;

  // Stack bytes in committed memory.
  uint64_t committed = 0;

  // Number of bytes in whole pages between the current top of the stack and the committed pages
  // (these bytes could theoretically be thrown away).
  uint64_t wasted = 0;
};

ThreadStackUsage GetThreadStackUsage(ConsoleContext* console_context,
                                     const std::vector<debug_ipc::AddressRegion>& map,
                                     Thread* thread);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STACK_USAGE_H_
