// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon/system/ulib/profile/config.h"

#include <fcntl.h>
#include <lib/fit/result.h>
#include <lib/syslog/global.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>
#include <zircon/syscalls/profile.h>

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

#include <fbl/unique_fd.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <re2/re2.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>

using zircon_profile::Profile;
using zircon_profile::ProfileScope;

namespace {

constexpr char kConfigFileExtension[] = ".profiles";

std::string ToString(const zx_profile_info_t& info) {
  std::ostringstream stream;

  stream << "{ ";

  if (info.flags & ZX_PROFILE_INFO_FLAG_PRIORITY) {
    stream << "\"priority\": " << info.priority << ", ";
  }
  if (info.flags & ZX_PROFILE_INFO_FLAG_DEADLINE) {
    stream << "\"capacity\": " << info.deadline_params.capacity
           << ", \"deadline\": " << info.deadline_params.relative_deadline
           << ", \"period\": " << info.deadline_params.period << ", ";
  }
  if (info.flags & ZX_PROFILE_INFO_FLAG_CPU_MASK) {
    stream << "\"affinity\": " << info.cpu_affinity_mask.mask[0] << " (0x" << std::hex
           << info.cpu_affinity_mask.mask[0] << "), " << std::dec;
  }

  stream << "}";
  return stream.str();
}

std::string ToString(ProfileScope scope) {
  switch (scope) {
    case ProfileScope::Bringup:
      return "bringup";
    case ProfileScope::Core:
      return "core";
    case ProfileScope::Product:
      return "product";
    default:
      return "none";
  }
}
// Proxies an iterator over the members of the given value node. As of this
// writing, the version of rapidjson in third_party does not support range-based
// for loops. This adapter provides the missing functionality.
struct IterateMembers {
  explicit IterateMembers(const rapidjson::Value& value)
      : begin_iterator{value.MemberBegin()}, end_iterator{value.MemberEnd()} {}

  auto begin() { return begin_iterator; }
  auto end() { return end_iterator; }

  rapidjson::Value::ConstMemberIterator begin_iterator;
  rapidjson::Value::ConstMemberIterator end_iterator;
};

// Proxies an iterator over the values of the given array node. Provides missing
// functionality similar to the iterator above.
struct IterateValues {
  explicit IterateValues(const rapidjson::Value& value)
      : begin_iterator{value.Begin()}, end_iterator{value.End()} {}

  auto begin() { return begin_iterator; }
  auto end() { return end_iterator; }

  rapidjson::Value::ConstValueIterator begin_iterator;
  rapidjson::Value::ConstValueIterator end_iterator;
};

// Utility to build a fit::result<std::string, ?> in the error state using stream operators.
//
// Example:
//
//   return Error() << "Failed to open file " << filename << "!";
//
class Error {
 public:
  Error() = default;
  explicit Error(const std::string& initial) : stream_{initial} {}

  // Forwards the stream operator argument to the underlying ostream.
  template <typename T>
  Error& operator<<(const T& value) {
    stream_ << value;
    return *this;
  }

  // Implicit conversion to fit::result<std::string, ?> in the error state with accumulated string
  // as the error value.
  template <typename... Vs>
  operator fit::result<std::string, Vs...>() const {
    return fit::error(stream_.str());
  }

  operator fit::error<std::string>() const { return fit::error(stream_.str()); }

 private:
  std::ostringstream stream_;
};

fit::result<std::string, zx::duration> ParseDurationString(const std::string& duration) {
  // Match one or more digits, optionally followed by time units ms, us, or ns.
  static const re2::RE2 kReDuration{"^(\\d+)(ms|us|ns)?$"};

  int64_t scalar;
  std::string units;
  const bool matched = re2::RE2::PartialMatch(duration, kReDuration, &scalar, &units);
  if (!matched) {
    return Error() << "String \"" << duration << "\" is not a valid duration!";
  }

  if (units.empty() || units == "ns") {
    return fit::ok(zx::nsec(scalar));
  }
  if (units == "ms") {
    return fit::ok(zx::msec(scalar));
  }
  if (units == "us") {
    return fit::ok(zx::usec(scalar));
  }
  return Error() << "String duration \"" << duration << "\" has unrecognized units \"" << units
                 << "\"!";
}

fit::result<std::string, zx::duration> ParseDuration(const rapidjson::Value& object) {
  if (object.IsInt()) {
    return fit::ok(zx::nsec(object.GetInt()));
  }
  if (object.IsString()) {
    return ParseDurationString(object.GetString());
  }
  return fit::error("Duration must be an integer or duration string!");
}

struct TextPosition {
  int32_t line;
  int32_t column;
};

TextPosition GetLineAndColumnForOffset(const std::string& input, size_t offset) {
  if (offset == 0) {
    // Errors at position 0 are assumed to be related to the whole file.
    return {.line = 0, .column = 0};
  }

  TextPosition position = {.line = 1, .column = 1};
  for (size_t i = 0; i < input.size() && i < offset; i++) {
    if (input[i] == '\n') {
      position.line += 1;
      position.column = 1;
    } else {
      position.column += 1;
    }
  }

  return position;
}

std::string GetErrorMessage(const rapidjson::Document& document, const std::string& file_data) {
  const auto [line, column] = GetLineAndColumnForOffset(file_data, document.GetErrorOffset());
  std::ostringstream stream;
  stream << line << ":" << column << ": " << GetParseError_En(document.GetParseError());
  return stream.str();
}

template <typename... Context>
auto GetMember(const char* name, const rapidjson::Value& object, Context&&... context)
    -> fit::result<std::string, decltype(std::cref(object[name]))> {
  if (!object.IsObject()) {
    return (Error() << ... << std::forward<Context>(context)) << " must be a JSON object!";
  }
  if (!object.HasMember(name)) {
    return (Error() << ... << std::forward<Context>(context))
           << " must have a \"" << name << "\" member!";
  }
  return fit::ok(std::cref(object[name]));
}

template <typename... Context>
fit::result<std::string, int> GetInt(const char* name, const rapidjson::Value& object,
                                     Context&&... context) {
  auto result = GetMember(name, object, std::forward<Context>(context)...);
  if (result.is_error()) {
    return result.take_error();
  }
  if (!result->get().IsInt()) {
    return (Error() << ... << std::forward<Context>(context))
           << " member \"" << name << "\" must be an integer!";
  }
  return fit::ok(result->get().GetInt());
}

template <typename... Context>
fit::result<std::string, const char*> GetString(const char* name, const rapidjson::Value& object,
                                                Context&&... context) {
  auto result = GetMember(name, object, std::forward<Context>(context)...);
  if (result.is_error()) {
    return result.take_error();
  }
  if (!result->get().IsString()) {
    return (Error() << ... << std::forward<Context>(context))
           << " member \"" << name << "\" must be a string!";
  }
  return fit::ok(result->get().GetString());
}

template <typename... Context>
auto GetArray(const char* name, const rapidjson::Value& object, Context&&... context) {
  auto result = GetMember(name, object, std::forward<Context>(context)...);
  if (result.is_ok() && !result->get().IsArray()) {
    return (Error() << ... << std::forward<Context>(context))
           << " member \"" << name << "\" must be an array!";
  }
  return result;
}

template <typename... Context>
auto GetObject(const char* name, const rapidjson::Value& object, Context&&... context) {
  auto result = GetMember(name, object, std::forward<Context>(context)...);
  if (result.is_ok() && !result->get().IsObject()) {
    return (Error() << ... << std::forward<Context>(context))
           << " member \"" << name << "\" must be a JSON object!";
  }
  return result;
}

template <typename... Context>
fit::result<std::string, unsigned int> GetUint(const char* name, const rapidjson::Value& object,
                                               Context&&... context) {
  auto result = GetMember(name, object, std::forward<Context>(context)...);
  if (result.is_error()) {
    return result.take_error();
  }
  if (!result->get().IsUint()) {
    return (Error() << ... << std::forward<Context>(context)).rdbuf()
           << " member \"" << name << "\" must be an unsigned integer!";
  }
  return fit::ok(result->get().GetUint());
}

void ParseProfiles(const std::string& filename, const rapidjson::Document& document,
                   zircon_profile::ProfileMap* profiles) {
  if (!document.IsObject()) {
    FX_LOG(WARNING, "ProfileProvider", "The profile config document must be a JSON object!");
    return;
  }

  if (!document.HasMember("profiles")) {
    return;
  }
  const rapidjson::Value& profile_member = document["profiles"];

  ProfileScope scope = ProfileScope::None;
  if (document.HasMember("scope")) {
    auto result = GetString("scope", document);
    if (result.is_ok()) {
      if (!strcmp(*result, "bringup")) {
        scope = ProfileScope::Bringup;
      } else if (!strcmp(*result, "core")) {
        scope = ProfileScope::Core;
      } else if (!strcmp(*result, "product")) {
        scope = ProfileScope::Product;
      } else {
        FX_LOGF(WARNING, "ProfileProvider", "%s: Invalid role scope \"%s\", defaulting to none!",
                filename.c_str(), *result);
      }
    }
  } else {
    FX_LOGF(WARNING, "ProfileProvider", "%s: Missing role scope, defaulting to none!",
            filename.c_str());
  }

  for (const auto& profile : IterateMembers(profile_member)) {
    const char* profile_name = profile.name.GetString();
    if (!profile.value.IsObject()) {
      FX_LOGF(WARNING, "ProfileProvider", "%s: Profile \"%s\" value must be a JSON object!",
              filename.c_str(), profile_name);
      continue;
    }

    const bool has_priority = profile.value.HasMember("priority");
    const bool has_capacity = profile.value.HasMember("capacity");
    const bool has_deadline = profile.value.HasMember("deadline");
    const bool has_period = profile.value.HasMember("period");
    const bool has_affinity = profile.value.HasMember("affinity");

    const bool has_complete_deadline = has_capacity && has_deadline && has_period;
    const bool has_some_deadline = has_capacity || has_deadline || has_period;

    zx_profile_info_t info{};
    if (has_priority && !has_some_deadline) {
      auto result = GetInt("priority", profile.value, "Profile ", profile_name);
      if (result.is_ok()) {
        info.flags = ZX_PROFILE_INFO_FLAG_PRIORITY;
        info.priority =
            std::clamp<int32_t>(result.value(), ZX_PRIORITY_LOWEST, ZX_PRIORITY_HIGHEST);
      } else {
        FX_LOGF(WARNING, "ProfileProvider", "\"%s\": %s", profile_name,
                result.error_value().c_str());
        continue;
      }
    } else if (!has_priority && has_complete_deadline) {
      auto capacity_result = ParseDuration(profile.value["capacity"]);
      if (capacity_result.is_error()) {
        FX_LOGF(WARNING, "ProfileProvider", "\"%s\": %s", profile_name,
                capacity_result.error_value().c_str());
        continue;
      }
      auto deadline_result = ParseDuration(profile.value["deadline"]);
      if (deadline_result.is_error()) {
        FX_LOGF(WARNING, "ProfileProvider", "\"%s\": %s", profile_name,
                deadline_result.error_value().c_str());
        continue;
      }
      auto period_result = ParseDuration(profile.value["period"]);
      if (period_result.is_error()) {
        FX_LOGF(WARNING, "ProfileProvider", "\"%s\": %s", profile_name,
                period_result.error_value().c_str());
        continue;
      }
      info.flags = ZX_PROFILE_INFO_FLAG_DEADLINE;
      info.deadline_params = zx_sched_deadline_params_t{.capacity = capacity_result->get(),
                                                        .relative_deadline = deadline_result->get(),
                                                        .period = period_result->get()};
    } else if (has_priority && has_some_deadline) {
      FX_LOGF(WARNING, "ProfileProvider",
              "%s: \"%s\": Priority and deadline parameters are mutually exclusive!",
              filename.c_str(), profile_name);
      continue;
    } else if (!has_priority && !has_complete_deadline && has_some_deadline) {
      FX_LOGF(
          WARNING, "ProfileProvider",
          "%s: \"%s\": Deadline profiles must specify \"capacity\", \"deadline\", and \"period\"!",
          filename.c_str(), profile_name);
      continue;
    }

    if (has_affinity) {
      const auto& affinity_member = profile.value["affinity"];
      const bool is_uint = affinity_member.IsUint64();
      const bool is_array = affinity_member.IsArray();

      info.flags |= ZX_PROFILE_INFO_FLAG_CPU_MASK;

      if (is_uint) {
        static_assert(std::numeric_limits<uint64_t>::digits <= ZX_CPU_SET_BITS_PER_WORD);
        info.cpu_affinity_mask.mask[0] = affinity_member.GetUint64();
      } else if (is_array) {
        size_t element_count = 0;
        bool failed = false;

        for (const auto& value : IterateValues(affinity_member)) {
          if (!value.IsUint()) {
            FX_LOGF(WARNING, "ProfileProvider",
                    "%s: \"%s\": Array element %zu of profile member \"affinity\" must be an "
                    "unsigned integer!",
                    filename.c_str(), profile_name, element_count);
            failed = true;
            break;
          }

          element_count++;
          const size_t cpu_number = value.GetUint();

          if (cpu_number >= ZX_CPU_SET_MAX_CPUS) {
            FX_LOGF(
                WARNING, "ProfileProvider",
                "%s: \"%s\": Profile member \"affinity\" must be an integer in the range [0, %u)!",
                filename.c_str(), profile_name, ZX_CPU_SET_MAX_CPUS);
            failed = true;
            break;
          }

          info.cpu_affinity_mask.mask[cpu_number / ZX_CPU_SET_BITS_PER_WORD] |=
              uint64_t{1} << (cpu_number % ZX_CPU_SET_BITS_PER_WORD);
        }
        if (failed) {
          continue;
        }
      } else {
        FX_LOGF(
            WARNING, "ProfileProvider",
            "%s: \"%s\": Profile member \"affinity\" must be a uint64 or an array of CPU indices!",
            filename.c_str(), profile_name);
        continue;
      }
    }  // if (has_affinity)

    if (info.flags == 0) {
      FX_LOGF(WARNING, "ProfileProvider", "%s: Ignoring empty profile \"%s\".", filename.c_str(),
              profile_name);
      continue;
    }

    const auto [iter, added] = profiles->emplace(profile_name, Profile{scope, info});
    if (!added) {
      const ProfileScope existing_scope = iter->second.scope;
      if (existing_scope >= scope) {
        FX_LOGF(WARNING, "ProfileProvider", "%s: Profile \"%s\" already exists at %s scope.",
                filename.c_str(), profile_name, ToString(existing_scope).c_str());
      } else if (iter->second.scope < scope) {
        FX_LOGF(INFO, "ProfileProvider", "%s: Profile \"%s\" overridden at %s scope.",
                filename.c_str(), profile_name, ToString(scope).c_str());
        iter->second = Profile{scope, info};
      }
    }
  }  // for (const auto& profile : IterateMembers(document))
}

}  // anonymous namespace

namespace zircon_profile {

fit::result<fit::failed, Role> ParseRoleSelector(const std::string& role_selector) {
  static const re2::RE2 kReRoleParts{"(\\w[\\w\\-]+(?:\\.\\w[\\w\\-]+)*)(?::(.+))?"};
  static const re2::RE2 kSelector{"(\\w[\\w\\-]+)(?:=([^,]+))?,?"};

  Role role;
  std::string selectors;
  if (!re2::RE2::FullMatch(role_selector, kReRoleParts, &role.name, &selectors)) {
    FX_LOGF(WARNING, "ProfileProvider", "Bad selector: %s", role_selector.c_str());
    return fit::failed{};
  }

  re2::StringPiece input{selectors};
  std::string key, value;
  while (re2::RE2::Consume(&input, kSelector, &key, &value)) {
    const auto [iter, added] = role.selectors.emplace(key, value);
    if (!added) {
      FX_LOGF(WARNING, "ProfileProvider", "Duplicate key in selector: %s=%s", key.c_str(),
              value.c_str());
    }
  }

  return fit::ok(std::move(role));
}

fit::result<fit::failed, MediaRole> MaybeMediaRole(const Role& role) {
  const auto realm_iter = role.selectors.find("realm");
  if (realm_iter == role.selectors.end() || realm_iter->second != "media") {
    FX_LOGF(INFO, "ProfileProvider", "Role \"%s\" is not a media role.", role.name.c_str());
    return fit::failed{};
  }

  const auto capacity_iter = role.selectors.find("capacity");
  const auto deadline_iter = role.selectors.find("deadline");
  if (capacity_iter == role.selectors.end() || deadline_iter == role.selectors.end()) {
    FX_LOGF(WARNING, "ProfileProvider", "Malformed media role \"%s\"!", role.name.c_str());
    return fit::failed{};
  }

  int64_t capacity;
  if (!re2::RE2::FullMatch(capacity_iter->second, "(\\d+)", &capacity)) {
    FX_LOGF(WARNING, "ProfileProvider", "Media role \"%s\" has invalid capacity selector: %s",
            role.name.c_str(), capacity_iter->second.c_str());
    return fit::failed{};
  }

  int64_t deadline;
  if (!re2::RE2::FullMatch(deadline_iter->second, "(\\d+)", &deadline)) {
    FX_LOGF(WARNING, "ProfileProvider", "Media role \"%s\" has invalid deadline selector: %s",
            role.name.c_str(), deadline_iter->second.c_str());
    return fit::failed{};
  }

  return fit::ok(MediaRole{.capacity = capacity, .deadline = deadline});
}

fit::result<std::string, ProfileMap> LoadConfigs(const std::string& config_path) {
  fbl::unique_fd dir_fd(openat(AT_FDCWD, config_path.c_str(), O_RDONLY | O_DIRECTORY));
  if (!dir_fd.is_valid()) {
    // A non-existent directory is not an error.
    FX_LOGF(WARNING, "ProfileProvider", "Failed to open config dir: %s", config_path.c_str());
    return fit::ok(ProfileMap{});
  }

  std::vector<std::string> dir_entries;
  if (!files::ReadDirContentsAt(dir_fd.get(), ".", &dir_entries)) {
    return Error() << "Could not read directory contents from path " << config_path.c_str()
                   << " error " << strerror(errno);
  }

  const auto filename_predicate = [](const std::string& filename) {
    const auto pos = filename.rfind(kConfigFileExtension);
    return pos != std::string::npos && pos == (filename.size() - std::strlen(kConfigFileExtension));
  };

  ProfileMap profiles;
  for (const auto& entry : dir_entries) {
    if (!files::IsFileAt(dir_fd.get(), entry) || !filename_predicate(entry)) {
      continue;
    }

    FX_LOGF(INFO, "ProfileProvider", "Loading config: %s", entry.c_str());

    std::string data;
    if (!files::ReadFileToStringAt(dir_fd.get(), entry, &data)) {
      FX_LOGF(WARNING, "ProfileProvider", "Failed to read file: %s", entry.c_str());
      continue;
    }

    rapidjson::Document document;
    const auto kFlags = rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag |
                        rapidjson::kParseIterativeFlag;
    document.Parse<kFlags>(data);

    if (document.HasParseError()) {
      FX_LOGF(WARNING, "ProfileProvider", "%s:%s", entry.c_str(),
              GetErrorMessage(document, data).c_str());
      continue;
    }

    ParseProfiles(entry, document, &profiles);
  }

  FX_LOGF(INFO, "ProfileProvider", "Loaded profiles:");
  for (const auto& [key, value] : profiles) {
    FX_LOGF(INFO, "ProfileProvider", "  %-32s %-10s %s", key.c_str(), ToString(value.scope).c_str(),
            ToString(value.info).c_str());
  }

  return fit::ok(std::move(profiles));
}

}  // namespace zircon_profile
