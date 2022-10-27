// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STEPS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STEPS_H_

#include <vector>

#include "src/developer/debug/zxdb/console/command_context.h"

namespace zxdb {

struct SubstatementCall;
class Thread;
struct VerbRecord;

VerbRecord GetStepsVerbRecord();

// Runs a "steps" with the given identified substatements. This is exposed for testing so it can be
// run with some canned substatements without having to mock the memory request which the
// substatement code uses.
void RunVerbStepsWithSubstatements(Thread* thread, std::vector<SubstatementCall> calls,
                                   fxl::RefPtr<CommandContext> cmd_context);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_STEPS_H_
