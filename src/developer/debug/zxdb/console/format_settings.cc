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
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

std::string SettingValueToString(const SettingValue& value) {
  switch (value.type()) {
    case SettingType::kBoolean:
      return value.GetBool() ? "true" : "false";
    case SettingType::kInteger:
      return fxl::StringPrintf("%d", value.GetInt());
    case SettingType::kString: {
      auto string = value.GetString();
      return string.empty() ? "<empty>" : string;
    }
    case SettingType::kList:
      // Lists are formatted as a colon separated string.
      // Example
      //    list = {"first", "second", "third"} -> "first:second:third"
      return fxl::JoinStrings(value.GetList(), ":");
    case SettingType::kNull:
      return "<null>";
  }
}

std::vector<std::string> ListToBullet(const std::vector<std::string>& list) {
  std::vector<std::string> output;
  output.reserve(list.size());
  auto bullet = GetBullet();
  for (const std::string& item : list)
    output.emplace_back(
        fxl::StringPrintf("%s %s", bullet.c_str(), item.c_str()));
  return output;
}

// |add_heading| refers whether it should show the setting name or just list the
// values.
void AddSettingToTable(const StoredSetting& setting,
                       std::vector<std::vector<OutputBuffer>>* rows,
                       bool add_heading = true) {
  // TODO(donosoc): We need to check what level the setting comes from so we can
  //                highlight it in the listing.
  if (!setting.value.is_list()) {
    // Normal values as just entered as key-value pairs.
    auto& row = rows->emplace_back();
    if (add_heading)
      row.emplace_back(setting.schema_item.name());
    row.emplace_back(SettingValueToString(setting.value));
  } else {
    // List get special treatment so that we can show them as bullet lists.
    // This make reading them much easier when the elements of the lists
    // are long (eg. paths).
    auto bullet_list = ListToBullet(setting.value.GetList());
    // Special case for empty list.
    if (bullet_list.empty()) {
      auto& row = rows->emplace_back();
      if (add_heading)
        row.emplace_back(setting.schema_item.name());
      row.emplace_back("<empty>");
    } else {
      for (size_t i = 0; i < bullet_list.size(); i++) {
        auto& row = rows->emplace_back();

        if (add_heading) {
          // The first entry has the setting name.
          auto title = i == 0 ? OutputBuffer(setting.schema_item.name())
                              : OutputBuffer();
          auto it = row.emplace_back(std::move(title));
        }
        row.emplace_back(std::move(bullet_list[i]));
      }
    }
  }
}

}  // namespace

Err FormatSetting(const SettingStore& store, const std::string& setting_name,
                  OutputBuffer* out) {
  if (!store.HasSetting(setting_name))
    return Err("Could not find setting \"%s\"", setting_name.c_str());

  auto setting = store.GetSetting(setting_name);

  out->Append({Syntax::kHeading, setting.schema_item.name()});
  out->Append(OutputBuffer("\n"));

  out->Append(setting.schema_item.description());
  out->Append(OutputBuffer("\n\n"));

  out->Append({Syntax::kHeading, "Type: "});
  out->Append(SettingTypeToString(setting.schema_item.type()));
  out->Append("\n\n");

  out->Append({Syntax::kHeading, "Value(s):\n"});
  out->Append(FormatSettingValue(setting));

  // List have a copy-paste value for setting the value.
  if (setting.value.is_list()) {
    out->Append("\n");
    out->Append({Syntax::kComment,
                 "See \"help set\" about using the set value for lists.\n"});
    out->Append(fxl::StringPrintf("Set value: %s",
                                  SettingValueToString(setting.value).c_str()));
    out->Append("\n");
  }
  return Err();
}

OutputBuffer FormatSettingValue(const StoredSetting& setting) {
  FXL_DCHECK(!setting.value.is_null());

  OutputBuffer out;
  std::vector<std::vector<OutputBuffer>> rows;
  AddSettingToTable(setting, &rows, false);
  FormatTable(std::vector<ColSpec>{1}, std::move(rows), &out);
  return out;
}

OutputBuffer FormatSettingStore(const SettingStore& store) {
  std::vector<std::vector<OutputBuffer>> rows;
  for (auto [key, item] : store.schema()->items()) {
    // Overriden settings are meant to be listen in another schema.
    if (item.overriden())
      continue;

    auto setting = store.GetSetting(key);
    FXL_DCHECK(!setting.value.is_null());

    AddSettingToTable(setting, &rows);
  }

  OutputBuffer table;
  FormatTable(std::vector<ColSpec>(3), rows, &table);
  return table;
}

const char* SettingSchemaLevelToString(SettingSchema::Level level) {
  // The return value here should be the noun name that the user will use to
  // refer to this setting, hence "global" for the System and "process" for the
  // Target.
  switch (level) {
    case SettingSchema::Level::kDefault:
      return "Default";
    case SettingSchema::Level::kSystem:
      return "Global";
    case SettingSchema::Level::kJob:
      return "Job";
    case SettingSchema::Level::kTarget:
      return "Process";
    case SettingSchema::Level::kThread:
      return "Thread";
  }

  // Just in case.
  return "<invalid>";
}

}  // namespace zxdb
