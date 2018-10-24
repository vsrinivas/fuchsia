// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_settings.h"

#include "garnet/bin/zxdb/client/setting_schema.h"
#include "garnet/bin/zxdb/client/setting_store.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/strings/join_strings.h"

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
    output.emplace_back(fxl::StringPrintf("%s %s", bullet.data(), item.data()));
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

OutputBuffer FormatSettingValue(const StoredSetting& setting) {
  FXL_DCHECK(!setting.value.is_null());

  OutputBuffer out;
  std::vector<std::vector<OutputBuffer>> rows;
  AddSettingToTable(setting, &rows, false);
  FormatTable(std::vector<ColSpec>{1}, std::move(rows), &out);
  return out;
}

OutputBuffer FormatSetting(const StoredSetting& setting) {
  FXL_DCHECK(!setting.value.is_null());

  OutputBuffer out;
  out.Append({Syntax::kHeading, setting.schema_item.name()});
  out.Append(OutputBuffer("\n"));

  out.Append(setting.schema_item.description());
  out.Append(OutputBuffer("\n\n"));

  out.Append({Syntax::kHeading, "Type: "});
  out.Append(SettingTypeToString(setting.schema_item.type()));
  out.Append("\n\n");

  out.Append({Syntax::kHeading, "Value(s):\n"});
  out.Append(FormatSettingValue(setting));

  // List have a copy-paste value for setting the value.
  if (setting.value.is_list()) {
    out.Append("\n");
    out.Append({Syntax::kComment,
                "See \"help set\" about using the set value for lists.\n"});
    out.Append(fxl::StringPrintf("Set value: %s",
                                 SettingValueToString(setting.value).data()));
    out.Append("\n");
  }

  return out;
}

Err FormatSettings(const SettingStore& store, const std::string& setting_name,
                   OutputBuffer* out) {
  // Check if we're asking for a particular setting.
  if (!setting_name.empty()) {
    StoredSetting setting = store.GetSetting(setting_name);
    if (setting.value.is_null()) {
      return Err("Could not find setting \"%s\" within the current context.",
                 setting_name.data());
    }

    *out = FormatSetting(setting);
    return Err();
  }

  // List all settings.
  std::vector<std::vector<OutputBuffer>> rows;
  for (auto& [key, setting] : store.GetSettings())
    AddSettingToTable(setting, &rows);

  OutputBuffer list_out;
  FormatTable(std::vector<ColSpec>(2), std::move(rows), &list_out);
  out->Append({Syntax::kComment,
               "Run get <option> to see detailed information.\n"});
  out->Append(std::move(list_out));
  return Err();
}

const char* SettingStoreLevelToString(SettingStore::Level level) {
  switch (level) {
    case SettingStore::Level::kDefault:
      return "default";
    case SettingStore::Level::kSystem:
      return "system";
    case SettingStore::Level::kTarget:
      return "target";
    case SettingStore::Level::kThread:
      return "thread";
  }

  // Just in case.
  return "<invalid>";
}

}  // namespace zxdb
