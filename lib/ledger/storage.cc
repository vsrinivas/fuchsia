// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/ledger/storage.h"

#include <vector>

#include "apps/modular/lib/util/string_escape.h"
#include "lib/ftl/strings/join_strings.h"

namespace modular {

// All message queue information is stored in one page.
// * 'MessageQueue/<queue_token>' => |MessageQueueInfo| describing the message
//   queue.
// * 'MessageQueueToken/<component_namespace>/<component_instance_id>/
//    <queue_name>' => queue token.
//
// All agent trigger information is stored in one page.
// * 'Trigger/agent_url/task_id' => |AgentRunner::TriggerInfo|
//
// Links are stored in the story page.
// * Link/<module path>/<link name> => link data
//   (<module path> is separated by ':')

constexpr char kEscaper = '\\';
constexpr char kCharsToEscape[] = ":/";
constexpr char kMessageQueueKeyPrefix[] = "MessageQueue/";
constexpr char kMessageQueueTokenKeyPrefix[] = "MessageQueueToken/";
constexpr char kLinkKeyPrefix[] = "Link/";
constexpr char kTriggerKeyPrefix[] = "Trigger/";

std::string MakeMessageQueueTokenKey(const std::string& component_namespace,
                                     const std::string& component_instance_id,
                                     const std::string& queue_name) {
  constexpr char kSeparator[] = "/";
  std::string key{kMessageQueueTokenKeyPrefix};
  key.append(
      StringEscape(component_namespace, kSeparator, kEscaper));
  key.append(kSeparator);
  key.append(
      StringEscape(component_instance_id, kSeparator, kEscaper));
  key.append(kSeparator);
  key.append(StringEscape(queue_name, kSeparator, kEscaper));
  return key;
}

std::string MakeMessageQueueKey(const std::string& queue_token) {
  // Don't need to escape |queue_token| because it is the only input to this
  // ledger key.
  return kMessageQueueKeyPrefix + queue_token;
}

std::string EncodeModulePath(const fidl::Array<fidl::String>& path) {
  std::vector<std::string> escaped_path;
  escaped_path.reserve(path.size());

  for (const auto& item : path) {
    escaped_path.push_back(StringEscape(item.get(), kCharsToEscape,
                                        kEscaper));
  }
  return ftl::JoinStrings(escaped_path, ":");
}

std::string EncodeModuleComponentNamespace(const std::string& story_id) {
  return "story:" + story_id;
}

std::string MakeLinkKey(const fidl::Array<fidl::String>& path,
                            const fidl::String& link_id) {
  std::string key{kLinkKeyPrefix};
  key.append(EncodeModulePath(path));
  key.append("/");
  key.append(StringEscape(link_id.get(), kCharsToEscape, kEscaper));

  return key;
}

std::string MakeTriggerKey(const std::string& agent_url,
                           const std::string& task_id) {
  std::string key{kTriggerKeyPrefix};
  key.append(StringEscape(agent_url, kCharsToEscape, kEscaper));
  key.append("/");
  key.append(StringEscape(task_id, kCharsToEscape, kEscaper));
  return key;
}

}  // namespace modular
