// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_GROUP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_GROUP_H_

namespace zxdb {

// Used to group similar commands in the help.
enum class CommandGroup {
  kNone,

  kAssembly,
  kBreakpoint,
  kGeneral,
  kProcess,
  kJob,
  kQuery,
  kStep,
  kSymbol,
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_GROUP_H_
