// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/user_shell_settings/user_shell_settings.h"

#include <cmath>

#include <lib/fxl/files/file.h>
#include <lib/fxl/logging.h>

#include "third_party/rapidjson/rapidjson/document.h"

namespace modular {

namespace {

constexpr char kDeviceShellConfigJsonPath[] =
    "/system/data/sysui/device_shell_config.json";
std::vector<UserShellSettings>* g_system_settings;

}  // namespace

namespace internal {

// Given a rapidjson::Value that is an object (i.e. a key-value map), look up
// |key| in the object and return the value associated with it.
template <typename T>
T GetObjectValue(const rapidjson::Value& object, const std::string& key);

// Specializes |GetObjectValue|, expecting the type of object[key] to be an JSON
// string. Returns "" if the key doesn't exist or isn't a JSON string.
template <>
std::string GetObjectValue(const rapidjson::Value& object,
                           const std::string& key) {
  if (!object.IsObject())
    return "";

  if (!object.HasMember(key))
    return "";

  const auto& field = object[key];

  if (!field.IsString())
    return "";

  return field.GetString();
}

// Specializes |GetObjectValue|, expecting the type of object[key] to be a _JSON
// string that represents a float_. (Note that the type is not expected to be a
// raw JSON number!).  Returns std::numeric_limits<float>::signaling_NaN() if
// the float couldn't be parsed from the string.
template <>
float GetObjectValue(const rapidjson::Value& object, const std::string& key) {
  auto str = GetObjectValue<std::string>(object, key);

  if (str.empty())
    return std::numeric_limits<float>::signaling_NaN();

  const char* const c_str = str.c_str();
  char* c_str_end;

  const float f = std::strtof(c_str, &c_str_end);
  if (f == HUGE_VAL || (f == 0.0f && c_str_end == c_str))
    return std::numeric_limits<float>::signaling_NaN();

  return f;
}

// Specializes |GetObjectValue|, expecting the type of object[key] to be a JSON
// string that represents a fuchsia::ui::policy::DisplayUsage. Returns kUnknown
// if the string couldn't be parsed.
template <>
fuchsia::ui::policy::DisplayUsage GetObjectValue(const rapidjson::Value& object,
                                                 const std::string& key) {
  auto str = GetObjectValue<std::string>(object, key);
  if (str.empty()) {
    return fuchsia::ui::policy::DisplayUsage::kUnknown;
  }

  // Keep in sync with
  // <https://fuchsia.googlesource.com/topaz/+/master/lib/device_shell/lib/user_shell_chooser.dart#64>.
  if (str == "handheld") {
    return fuchsia::ui::policy::DisplayUsage::kHandheld;
  } else if (str == "close") {
    return fuchsia::ui::policy::DisplayUsage::kClose;
  } else if (str == "near") {
    return fuchsia::ui::policy::DisplayUsage::kNear;
  } else if (str == "midrange") {
    return fuchsia::ui::policy::DisplayUsage::kMidrange;
  } else if (str == "far") {
    return fuchsia::ui::policy::DisplayUsage::kFar;
  } else {
    FXL_LOG(WARNING) << "unknown display usage string: " << str;
    return fuchsia::ui::policy::DisplayUsage::kUnknown;
  }
};

// Given a |json| string, parses it into list of user shell settings.
std::vector<UserShellSettings> ParseUserShellSettings(const std::string& json) {
  // TODO(MI4-1166): topaz/lib/device_shell/lib/user_shell_chooser.dart is a
  // similar implementation of this in Dart. One of the two implementations
  // could probably be removed now.

  std::vector<UserShellSettings> settings;

  rapidjson::Document document;
  document.Parse(json.c_str());
  if (document.HasParseError()) {
    FXL_LOG(ERROR) << "ParseUserShellSettings(): parse error "
                   << document.GetParseError();
    return settings;
  }

  if (!document.IsArray()) {
    FXL_LOG(ERROR) << "ParseUserShellSettings(): root item isn't an array";
    return settings;
  }

  if (document.Empty()) {
    FXL_LOG(ERROR) << "ParseUserShellSettings(): root array is empty";
    return settings;
  }

  settings.reserve(document.Size());

  for (rapidjson::SizeType i = 0; i < document.Size(); i++) {
    using modular::internal::GetObjectValue;

    const auto& user_shell = document[i];

    const auto& name = GetObjectValue<std::string>(user_shell, "name");
    if (name.empty())
      continue;

    settings.push_back({
        .name = GetObjectValue<std::string>(user_shell, "name"),
        .screen_width = GetObjectValue<float>(user_shell, "screen_width"),
        .screen_height = GetObjectValue<float>(user_shell, "screen_height"),
        .display_usage = GetObjectValue<fuchsia::ui::policy::DisplayUsage>(
            user_shell, "display_usage"),
    });
  }

  return settings;
}

}  // namespace internal

const std::vector<UserShellSettings>& UserShellSettings::GetSystemSettings() {
  // This method is intentionally thread-hostile to keep things simple, and
  // needs modification to be thread-safe or thread-compatible.

  if (g_system_settings) {
    return *g_system_settings;
  }

  g_system_settings = new std::vector<UserShellSettings>;

  std::string json;
  if (!files::ReadFileToString(kDeviceShellConfigJsonPath, &json)) {
    FXL_LOG(ERROR) << kDeviceShellConfigJsonPath << ": read failed";

    return *g_system_settings;
  }

  *g_system_settings = internal::ParseUserShellSettings(json);
  return *g_system_settings;
}

bool operator==(const UserShellSettings& lhs, const UserShellSettings& rhs) {
  if (lhs.name != rhs.name)
    return false;

  auto float_eq = [](float f, float g) {
    // OK to do direct float comparison here, since we want bitwise value
    // equality.
    if (f == g)
      return true;

    if (std::isnan(f) && std::isnan(g))
      return true;

    return false;
  };

  if (!float_eq(lhs.screen_width, rhs.screen_width))
    return false;

  if (!float_eq(lhs.screen_height, rhs.screen_height))
    return false;

  if (lhs.display_usage != rhs.display_usage)
    return false;

  return true;
}

}  // namespace modular
