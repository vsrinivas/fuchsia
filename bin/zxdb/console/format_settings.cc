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

OutputBuffer FormatSetting(const StoredSetting&) {
  // TODO(donosoc): Do the in detail setting formatting.
  FXL_NOTREACHED() << "NOT IMPLEMENTED";
  return OutputBuffer();
}

void AddSettingToTable(const StoredSetting& setting,
                       std::vector<std::vector<OutputBuffer>>* rows) {
  // TODO(donosoc): We need to check what level the setting comes from so we can
  //                highlight it in the listing.

  if (!setting.value.is_list()) {
    // Normal values as just entered as key-value pairs.
    auto& row = rows->emplace_back();
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
      row.emplace_back(setting.schema_item.name());
      row.emplace_back("<empty>");
    } else {
      for (size_t i = 0; i < bullet_list.size(); i++) {
        auto& row = rows->emplace_back();

        // The first entry has the setting name.
        auto title = i == 0 ? OutputBuffer(setting.schema_item.name())
                            : OutputBuffer();
        auto it = row.emplace_back(std::move(title));
        row.emplace_back(std::move(bullet_list[i]));
      }
    }
  }
}

}  // namespace

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

  FormatTable(std::vector<ColSpec>(2), std::move(rows), out);
  return Err();
}

}  // namespace zxdb
