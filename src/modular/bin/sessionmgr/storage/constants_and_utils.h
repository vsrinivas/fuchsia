// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is a description of the pages and keys used by the modular runtime.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_CONSTANTS_AND_UTILS_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_CONSTANTS_AND_UTILS_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/string.h>

#include <string>

namespace modular {
//
// 3. The trigger page contains the trigger conditions for all agents.
//
constexpr char kAgentRunnerPageId[] = "AgentRunnerPage_";  // 16 chars
//
// 4. Story pages (one per story) contain the story state expressed as link and
//    module data.

// Keys in these pages are constructed as follows:
//
// 1. A prefix indicates the kind of information stored under the key. The
//    prefix ends in a slash. The prefix is used to construct keys for reading
//    and writing, and to filter keys for bulk reading and in change
//    notifications.
//
//    Root page:
constexpr char kStoryKeyPrefix[] = "Story/";
constexpr char kDeviceKeyPrefix[] = "Device/";
constexpr char kFocusKeyPrefix[] = "Focus/";

// Keys under kStoryKeyPrefix
constexpr char kStoryDataKeyPrefix[] = "Story/Data/";

//    fuchsia::modular::Agent Trigger page:
constexpr char kTriggerKeyPrefix[] = "Trigger/";
//
//    Story page:
constexpr char kLinkKeyPrefix[] =
    "fuchsia::modular::Link|3/";  // version 3: no more incremental links
constexpr char kModuleKeyPrefix[] = "Module/";

// 2. ID values, separated by slashes, to identify the data item under this
//    key. The set of ID values under each key is defined by the arguments of
//    factory functions for the keys:
//
std::string MakeDeviceKey(const fidl::StringPtr& device_name);
std::string MakeFocusKey(const fidl::StringPtr& device_name);
std::string MakeTriggerKey(const std::string& agent_url, const std::string& task_id);
std::string MakeLinkKey(const fuchsia::modular::LinkPathPtr& link_path);
std::string MakeLinkKey(const fuchsia::modular::LinkPath& link_path);
std::string MakeModuleKey(const std::vector<std::string>& module_path);

// 3. The slash separator is escaped by a backslash inside the ID
//    values. Backslashes inside the ID values are escaped by backslash too.
//
constexpr char kSeparator[] = "/";
constexpr char kEscaper = '\\';
constexpr char kCharsToEscape[] = ":/";

// 4. The ID values may have internal structure on their own too, expressed by a
//    second sub separator character.
//
constexpr char kSubSeparator[] = ":";

std::string EncodeLinkPath(const fuchsia::modular::LinkPath& link_path);
std::string EncodeModulePath(const std::vector<std::string>& module_path);
std::string EncodeModuleComponentNamespace(const std::string& story_id);

// More notes:
//
// * Although keys can be parsed, the information encoded in the keys is usually
//   repeated in the value, and thus can be obtained without parsing the
//   key. This is the preferred way, as it leaves the possibility open to
//   replace key components with hashes.
//
// * The values under all keys are JSON. The structure of the JSON is defined by
//   Xdr*() functions to be found in the page access code.

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_CONSTANTS_AND_UTILS_H_
