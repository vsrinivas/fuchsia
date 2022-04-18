// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_ATTACH_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_ATTACH_H_

#include <stddef.h>

namespace zxdb {

// This should match ZX_MAX_NAME_LEN-1, but we don't want to include zircon headers here.
constexpr size_t kZirconMaxNameLength = 31;

struct VerbRecord;

VerbRecord GetAttachVerbRecord();

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_VERB_ATTACH_H_
