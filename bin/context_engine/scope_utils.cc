// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <openssl/sha.h>
#include <regex>
#include <sstream>

#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "lib/ftl/logging.h"

namespace maxwell {

namespace {

std::string ToHex(const std::string& s) {
  const char kHexChars[] = "0123456789abcdef";
  std::string r;
  r.reserve(s.size() * 2);
  for (unsigned char c : s) {
    r.push_back(kHexChars[c >> 4]);
    r.push_back(kHexChars[c & 0xf]);
  }
  return r;
}

// Generates a short Module ID from a Module's path by taking a SHA1 hash
// prefix. There are so few Modules in a story that a short hash prefix will be
// unique.
const int kHashPrefixLength = 5;
std::string ModulePathShortHash(const fidl::Array<fidl::String>& module_path) {
  FTL_CHECK(!module_path.is_null() && !module_path.empty());

  SHA_CTX sha_ctx;
  if (SHA1_Init(&sha_ctx) != 1) {
    FTL_LOG(FATAL) << "Could not SHA1_Init().";
  }
  for (const auto& part : module_path) {
    FTL_LOG(INFO) << "About to SHA1 " << part;
    FTL_CHECK(SHA1_Update(&sha_ctx, part.data(), part.size()) == 1);
  }

  unsigned char hash[SHA_DIGEST_LENGTH];
  FTL_CHECK(SHA1_Final(hash, &sha_ctx) == 1);
  return ToHex(
             std::string(reinterpret_cast<char*>(&hash[0]), SHA_DIGEST_LENGTH))
      .substr(0, kHashPrefixLength);
}

bool HasSlash(const std::string topic) {
  return !topic.empty() && topic[0] == '/';
}

}  // namespace

std::string ConcatTopic(const std::string t1, const std::string t2) {
  std::ostringstream out;
  out << t1 << (HasSlash(t2) ? "" : "/") << t2;
  return out.str();
}

std::string ScopeAndTopicToString(const ComponentScopePtr& scope,
                                  const std::string& topic) {
  if (scope->is_module_scope()) {
    const auto& module_scope = scope->get_module_scope();
    return MakeModuleScopeTopic(module_scope->story_id,
                                ModulePathShortHash(module_scope->module_path),
                                topic);
  }
  return topic;
}

std::string MakeStoryScopeTopic(const std::string& story_id,
                                const std::string& topic) {
  FTL_DCHECK(!story_id.empty());
  FTL_DCHECK(!topic.empty());
  std::ostringstream out;
  out << "/story/id/" << story_id << (HasSlash(topic) ? "" : "/") << topic;
  return out.str();
}

std::string MakeModuleScopeTopic(const std::string& story_id,
                                 const std::string& module_id,
                                 const std::string& topic) {
  FTL_DCHECK(!story_id.empty());
  FTL_DCHECK(!module_id.empty());
  FTL_DCHECK(!topic.empty());
  std::ostringstream out;
  out << "/story/id/" << story_id << "/module/" << module_id
      << (HasSlash(topic) ? "" : "/") << topic;
  return out.str();
}

std::string ModulePathToString(const fidl::Array<fidl::String>& module_path) {
  std::ostringstream ss;
  bool first = true;
  for (const auto& part : module_path) {
    if (!first) {
      ss << ':';
    }
    first = false;
    ss << part;
  }
  return ss.str();
}

std::string MakeModuleScopeTopic(const std::string& story_id,
                                 const fidl::Array<fidl::String>& module_path,
                                 const std::string& topic) {
  return MakeModuleScopeTopic(story_id, ModulePathShortHash(module_path),
                              topic);
}

std::string MakeFocusedStoryScopeTopic(const std::string& topic) {
  std::ostringstream out;
  out << "/story/focused" << (HasSlash(topic) ? "" : "/") << topic;
  return out.str();
}

bool ParseStoryScopeTopic(const std::string& full_topic,
                          std::string* story_id,
                          std::string* relative_topic) {
  static std::regex re("/story/id/([^/]+)/(.+)");
  std::smatch match;
  if (std::regex_match(full_topic, match, re)) {
    *story_id = match.str(1);
    *relative_topic = match.str(2);
    return true;
  }
  return false;
}

bool ParseModuleScopeTopic(const std::string& full_topic,
                           std::string* story_id,
                           std::string* module_id,
                           std::string* relative_topic) {
  std::regex re("/story/id/([^/]+)/module/([^/]+)/(.+)");
  std::smatch match;
  if (std::regex_match(full_topic, match, re)) {
    *story_id = match.str(1);
    *module_id = match.str(2);
    *relative_topic = match.str(3);
    return true;
  }
  return false;
}

}  // namespace maxwell
