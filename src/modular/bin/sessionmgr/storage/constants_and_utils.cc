// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"

#include <string>
#include <vector>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/modular/lib/string_escape/string_escape.h"

namespace modular {

std::string MakeDeviceKey(const fidl::StringPtr& device_name) {
  // Not escaped, because only one component after the prefix.
  return kDeviceKeyPrefix + device_name.value_or("");
}

std::string MakeFocusKey(const fidl::StringPtr& device_name) {
  // Not escaped, because only one component after the prefix.
  return kFocusKeyPrefix + device_name.value_or("");
}

std::string EncodeModulePath(const std::vector<std::string>& module_path) {
  std::vector<std::string> segments;
  segments.reserve(module_path.size());
  for (const auto& module_path_part : module_path) {
    segments.emplace_back(StringEscape(module_path_part, kCharsToEscape, kEscaper));
  }
  return fxl::JoinStrings(segments, kSubSeparator);
}

std::string EncodeLinkPath(const fuchsia::modular::LinkPath& link_path) {
  std::string output;
  output.append(EncodeModulePath(link_path.module_path));
  output.append(kSeparator);
  output.append(StringEscape(link_path.link_name.value_or(""), kCharsToEscape, kEscaper));
  return output;
}

std::string EncodeModuleComponentNamespace(const std::string& story_id) {
  // TODO(mesch): Needs escaping, and must not be escaped when used as component
  // of a full key. Messy.
  return "story:" + story_id;
}

std::string MakeTriggerKey(const std::string& agent_url, const std::string& task_id) {
  std::string key{kTriggerKeyPrefix};
  key.append(StringEscape(agent_url, kCharsToEscape, kEscaper));
  key.append(kSeparator);
  key.append(StringEscape(task_id, kCharsToEscape, kEscaper));
  return key;
}

std::string MakeLinkKey(const fuchsia::modular::LinkPathPtr& link_path) {
  return MakeLinkKey(*link_path);
}

std::string MakeLinkKey(const fuchsia::modular::LinkPath& link_path) {
  std::string key{kLinkKeyPrefix};
  key.append(EncodeLinkPath(link_path));
  return key;
}

std::string MakeModuleKey(const std::vector<std::string>& module_path) {
  FXL_DCHECK(module_path.size() > 0) << EncodeModulePath(module_path);
  FXL_DCHECK(module_path.at(0).size() > 0) << EncodeModulePath(module_path);
  std::string key{kModuleKeyPrefix};
  key.append(EncodeModulePath(module_path));
  return key;
}

}  // namespace modular
