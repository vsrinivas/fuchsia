// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/session_shell_settings/session_shell_settings.h"

#include <cmath>

#include "src/lib/files/file.h"
#include <src/lib/fxl/logging.h>

#include "rapidjson/document.h"

namespace modular {

namespace {

constexpr char kBaseShellConfigJsonPath[] =
    "/system/data/sysui/base_shell_config.json";
std::vector<SessionShellSettings>* g_system_settings;

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
  // <https://fuchsia.googlesource.com/topaz/+/master/lib/base_shell/lib/session_shell_chooser.dart#64>.
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

// Given a |json| string, parses it into list of session shell settings.
std::vector<SessionShellSettings> ParseSessionShellSettings(
    const std::string& json) {
  std::vector<SessionShellSettings> settings;

  rapidjson::Document document;
  document.Parse(json.c_str());
  if (document.HasParseError()) {
    FXL_LOG(ERROR) << "ParseSessionShellSettings(): parse error "
                   << document.GetParseError();
    return settings;
  }

  if (!document.IsArray()) {
    FXL_LOG(ERROR) << "ParseSessionShellSettings(): root item isn't an array";
    return settings;
  }

  if (document.Empty()) {
    FXL_LOG(ERROR) << "ParseSessionShellSettings(): root array is empty";
    return settings;
  }

  settings.reserve(document.Size());

  for (rapidjson::SizeType i = 0; i < document.Size(); i++) {
    using modular::internal::GetObjectValue;

    const auto& session_shell = document[i];

    const auto& name = GetObjectValue<std::string>(session_shell, "name");
    if (name.empty())
      continue;

    settings.push_back({
        .name = GetObjectValue<std::string>(session_shell, "name"),
        .screen_width = GetObjectValue<float>(session_shell, "screen_width"),
        .screen_height = GetObjectValue<float>(session_shell, "screen_height"),
        .display_usage = GetObjectValue<fuchsia::ui::policy::DisplayUsage>(
            session_shell, "display_usage"),
    });
  }

  return settings;
}

}  // namespace internal

const std::vector<SessionShellSettings>&
SessionShellSettings::GetSystemSettings() {
  // This method is intentionally thread-hostile to keep things simple, and
  // needs modification to be thread-safe or thread-compatible.

  if (g_system_settings) {
    return *g_system_settings;
  }

  g_system_settings = new std::vector<SessionShellSettings>;

  std::string json;
  if (!files::ReadFileToString(kBaseShellConfigJsonPath, &json)) {
    FXL_LOG(ERROR) << kBaseShellConfigJsonPath << ": read failed";

    return *g_system_settings;
  }

  *g_system_settings = internal::ParseSessionShellSettings(json);
  return *g_system_settings;
}

bool operator==(const SessionShellSettings& lhs,
                const SessionShellSettings& rhs) {
  auto float_eq = [](float f, float g) {
    // OK to do direct float comparison here, since we want bitwise value
    // equality.
    if (f == g)
      return true;

    if (std::isnan(f) && std::isnan(g))
      return true;

    return false;
  };

  return lhs.name == rhs.name && float_eq(lhs.screen_width, rhs.screen_width) &&
         float_eq(lhs.screen_height, rhs.screen_height) &&
         lhs.display_usage == rhs.display_usage;
}

}  // namespace modular
