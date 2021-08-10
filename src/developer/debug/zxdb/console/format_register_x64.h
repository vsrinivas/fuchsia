// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_X64_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_X64_H_

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/register_info.h"
#include "src/developer/debug/shared/register_value.h"

namespace zxdb {

struct FormatRegisterOptions;
class OutputBuffer;

// Does x64-specific formatting of the registesrs of a given category. Returns true if this category
// was handled. False means their is no special X64 handling for this category.
bool FormatCategoryX64(const FormatRegisterOptions& options, debug::RegisterCategory category,
                       const std::vector<debug::RegisterValue>& registers, OutputBuffer* out);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_X64_H_
