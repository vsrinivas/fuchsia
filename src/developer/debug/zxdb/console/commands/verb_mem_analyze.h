// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_MEM_ANALYZE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_MEM_ANALYZE_H_

#include <optional>

namespace zxdb {

class Command;
class Err;
struct VerbRecord;

VerbRecord GetMemAnalyzeVerbRecord();

// Parsing of the "size" and "num" switches is shared with the "stack" command which is a variant of
// the analyze command.
extern const int kMemAnalyzeSizeSwitch;
extern const int kMemAnalyzeNumSwitch;

// Reads the two switches above. The |*out_size| will be unchanged if neither is specified.
Err ReadAnalyzeNumAndSizeSwitches(const Command& cmd, std::optional<uint32_t>* out_size);

extern const uint32_t kDefaultAnalyzeByteSize;

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_MEM_ANALYZE_H_
