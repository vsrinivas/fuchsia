// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_settings.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

OutputBuffer SettingValueToString(ConsoleContext* context, const SettingValue& value) {
  switch (value.type()) {
    case SettingType::kBoolean: {
      return value.get_bool() ? OutputBuffer("true") : OutputBuffer("false");
    }
    case SettingType::kInteger: {
      return fxl::StringPrintf("%d", value.get_int());
    }
    case SettingType::kString: {
      auto string = value.get_string();
      return string.empty() ? OutputBuffer(Syntax::kComment, "\"\"")
                            : OutputBuffer(FormatConsoleString(string));
    }
    case SettingType::kList: {
      const auto& list = value.get_list();
      std::string result;
      for (size_t i = 0; i < list.size(); i++) {
        if (i > 0)
          result += " ";
        result += FormatConsoleString(list[i]);
      }
      return OutputBuffer(result);
    }
    case SettingType::kExecutionScope: {
      return ExecutionScopeToString(context, value.get_execution_scope());
    }
    case SettingType::kInputLocations: {
      return FormatInputLocations(value.get_input_locations());
    }
    case SettingType::kNull: {
      return OutputBuffer(Syntax::kComment, "<null>");
    }
  }
}

std::vector<std::string> ListToBullet(const std::vector<std::string>& list) {
  std::vector<std::string> output;
  output.reserve(list.size());
  auto bullet = GetBullet();
  for (const std::string& item : list) {
    output.emplace_back(
        fxl::StringPrintf("%s %s", bullet.c_str(), FormatConsoleString(item).c_str()));
  }
  return output;
}

// |add_heading| refers whether it should show the setting name or just list the values.
void AddSettingToTable(ConsoleContext* context, const Setting& setting,
                       std::vector<std::vector<OutputBuffer>>* rows, bool add_heading) {
  // TODO(donosoc): We need to check what level the setting comes from so we can highlight it in the
  //                listing.
  if (!setting.value.is_list()) {
    // Normal values as just entered as key-value pairs.
    auto& row = rows->emplace_back();
    if (add_heading)
      row.emplace_back(Syntax::kVariable, setting.info.name);
    row.emplace_back(SettingValueToString(context, setting.value));
  } else {
    // List get special treatment so that we can show them as bullet lists. This make reading them
    // much easier when the elements of the lists are long (eg. paths).
    auto bullet_list = ListToBullet(setting.value.get_list());
    // Special case for empty list.
    if (bullet_list.empty()) {
      auto& row = rows->emplace_back();
      if (add_heading)
        row.emplace_back(Syntax::kVariable, setting.info.name);
      row.emplace_back(Syntax::kComment, "<empty>");
    } else {
      for (size_t i = 0; i < bullet_list.size(); i++) {
        auto& row = rows->emplace_back();

        if (add_heading) {
          // The first entry has the setting name.
          auto title = i == 0 ? OutputBuffer(Syntax::kVariable, setting.info.name) : OutputBuffer();
          auto it = row.emplace_back(std::move(title));
        }
        row.emplace_back(std::move(bullet_list[i]));
      }
    }
  }
}

}  // namespace

OutputBuffer FormatSettingStore(ConsoleContext* context, const SettingStore& store) {
  std::vector<std::vector<OutputBuffer>> rows;
  for (auto [key, _] : store.schema()->settings()) {
    auto setting = store.GetSetting(key);
    FXL_DCHECK(!setting.value.is_null());
    AddSettingToTable(context, setting, &rows, true);
  }

  OutputBuffer table;
  FormatTable({ColSpec(Align::kLeft, 0, std::string(), 2), ColSpec()}, rows, &table);
  return table;
}

OutputBuffer FormatSetting(ConsoleContext* context, const Setting& setting) {
  OutputBuffer out;
  out.Append(Syntax::kVariable, setting.info.name);
  out.Append(OutputBuffer("\n"));

  out.Append(setting.info.description);
  out.Append(OutputBuffer("\n\n"));

  out.Append(Syntax::kHeading, "Type: ");
  out.Append(SettingTypeToString(setting.value.type()));
  out.Append("\n\n");

  out.Append(Syntax::kHeading, "Value(s):\n");
  out.Append(FormatSettingShort(context, setting));

  // List have a copy-paste value for setting the value.
  if (setting.value.is_list()) {
    out.Append("\n");
    out.Append(Syntax::kComment, "See \"help set\" about using the set value for lists.\n");
    out.Append(Syntax::kComment,
               fxl::StringPrintf("To set, type: set %s ", setting.info.name.c_str()));
    out.Append(SettingValueToString(context, setting.value));
    out.Append("\n");
  }

  return out;
}

OutputBuffer FormatSettingShort(ConsoleContext* context, const Setting& setting) {
  FXL_DCHECK(!setting.value.is_null());

  OutputBuffer out;
  std::vector<std::vector<OutputBuffer>> rows;
  AddSettingToTable(context, setting, &rows, false);
  FormatTable(std::vector<ColSpec>{1}, std::move(rows), &out);
  return out;
}

}  // namespace zxdb
