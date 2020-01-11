// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SETTINGS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SETTINGS_H_

#include <string>

#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/developer/debug/zxdb/client/setting_value.h"

namespace zxdb {

class Command;
class ConsoleContext;
class Err;
class OutputBuffer;

OutputBuffer FormatSettingStore(ConsoleContext* context, const SettingStore& store);

// Outputs the detailed information about a particular setting.
OutputBuffer FormatSetting(ConsoleContext* context, const std::string& name,
                           const std::string& description, const SettingValue& value);

// Formats the setting to just show the value. Since lists go on separate different lines,
// list_indent can be used to insert spaces to the left of each.
OutputBuffer FormatSettingShort(ConsoleContext* context, const std::string& name,
                                const SettingValue& value, int list_indent = 0);

// Formats an individual setting value. This is the low-level formatting and
// doesn't do any special handling for lists (it will be just space-separated).
OutputBuffer FormatSettingValue(ConsoleContext* context, const SettingValue& value);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SETTINGS_H_
