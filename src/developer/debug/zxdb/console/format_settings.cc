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
void AddSettingToTable(ConsoleContext* context, const std::string& name, const SettingValue& value,
                       std::vector<std::vector<OutputBuffer>>* rows, bool add_heading) {
  // TODO(donosoc): We need to check what level the setting comes from so we can highlight it in the
  //                listing.
  if (!value.is_list()) {
    // Normal values as just entered as key-value pairs.
    auto& row = rows->emplace_back();
    if (add_heading)
      row.emplace_back(Syntax::kVariable, name);
    row.emplace_back(FormatSettingValue(context, value));
  } else {
    // List get special treatment so that we can show them as bullet lists. This make reading them
    // much easier when the elements of the lists are long (eg. paths).
    auto bullet_list = ListToBullet(value.get_list());
    // Special case for empty list.
    if (bullet_list.empty()) {
      auto& row = rows->emplace_back();
      if (add_heading)
        row.emplace_back(Syntax::kVariable, name);
      row.emplace_back(Syntax::kComment, "<empty>");
    } else {
      for (size_t i = 0; i < bullet_list.size(); i++) {
        auto& row = rows->emplace_back();

        if (add_heading) {
          // The first entry has the setting name.
          auto title = i == 0 ? OutputBuffer(Syntax::kVariable, name) : OutputBuffer();
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
    auto value = store.GetValue(key);
    FXL_DCHECK(!value.is_null());
    AddSettingToTable(context, key, value, &rows, true);
  }

  OutputBuffer table;
  FormatTable({ColSpec(Align::kLeft, 0, std::string(), 2), ColSpec()}, rows, &table);
  return table;
}

OutputBuffer FormatSetting(ConsoleContext* context, const std::string& name,
                           const std::string& description, const SettingValue& value) {
  // Heading, type, and help description.
  OutputBuffer out;
  out.Append(Syntax::kHeading, name);
  out.Append(Syntax::kComment, fxl::StringPrintf(" (%s)\n\n", SettingTypeToString(value.type())));

  out.Append(description);
  out.Append(OutputBuffer("\n\n"));

  out.Append(Syntax::kVariable, name);
  out.Append(" = ");

  // Nonempty lists are written on the following line. Everything else goes on the same line.
  if (value.is_list() && !value.get_list().empty())
    out.Append("\n");
  out.Append(FormatSettingShort(context, name, value, 2));

  if (value.is_list()) {
    // List have a copy-paste value for setting the value.
    out.Append("\n");
    out.Append(Syntax::kComment, "See \"help set\" about using the set value for lists.\n");
    out.Append(Syntax::kComment, fxl::StringPrintf("To set, type: set %s ", name.c_str()));
    out.Append(Syntax::kComment, FormatSettingValue(context, value).AsString().c_str());
    out.Append("\n");
  }

  return out;
}

OutputBuffer FormatSettingShort(ConsoleContext* context, const std::string& name,
                                const SettingValue& value, int list_indent) {
  FXL_DCHECK(!value.is_null());

  int pad_left = value.is_list() ? list_indent : 0;

  OutputBuffer out;
  std::vector<std::vector<OutputBuffer>> rows;
  AddSettingToTable(context, name, value, &rows, false);
  FormatTable(std::vector<ColSpec>{ColSpec(Align::kLeft, 0, std::string(), pad_left)},
              std::move(rows), &out);
  return out;
}

OutputBuffer FormatSettingValue(ConsoleContext* context, const SettingValue& value) {
  switch (value.type()) {
    case SettingType::kBoolean: {
      return OutputBuffer(BoolToString(value.get_bool()));
    }
    case SettingType::kInteger: {
      return std::to_string(value.get_int());
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

}  // namespace zxdb
