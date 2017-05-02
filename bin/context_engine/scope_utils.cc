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

// A short-term solution to generate a "Module ID" from the URL by computing a
// SHA1 hash.  TODO(thatguy): Prefer to use the Module ID assigned by the
// Framework explicitly.
std::string GetModuleId(const std::string& module_url) {
  SHA_CTX sha_ctx;
  if (SHA1_Init(&sha_ctx) != 1) {
    FTL_LOG(FATAL) << "Could not SHA1_Init().";
  }
  if (SHA1_Update(&sha_ctx, module_url.data(), module_url.size()) != 1) {
    FTL_LOG(FATAL) << "Could not SHA1_Update() for string \"" << module_url
                   << "\".";
  }
  unsigned char hash[SHA_DIGEST_LENGTH];
  if (SHA1_Final(hash, &sha_ctx) != 1) {
    FTL_LOG(FATAL) << "Could not SHA1_Final() for string \"" << module_url
                   << "\".";
  }
  return ToHex(
      std::string(reinterpret_cast<char*>(&hash[0]), SHA_DIGEST_LENGTH));
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
                                GetModuleId(module_scope->url), topic);
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
