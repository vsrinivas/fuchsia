// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_DOWN_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_DOWN_H_

namespace zxdb {

class Frame;
struct VerbRecord;

VerbRecord GetDownVerbRecord();

// Prints the message for up/down commands.
void OutputFrameInfoForChange(const Frame* frame, int id);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_DOWN_H_
