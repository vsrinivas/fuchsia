// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/coprocessors/focused_story.h"

#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace maxwell {

namespace {

const char kFocusedStoryTopic[] = "/story/focused_id";
const char kJsonNull[] = "null";

// Returns an empty string if no Story is currently focused. Otherwise
// returns the ID.
std::string GetFocusedStoryId(const ContextRepository* repository) {
  const std::string* focused_story_ptr = repository->Get(kFocusedStoryTopic);
  if (focused_story_ptr == nullptr)
    return "";

  rapidjson::Document d;
  d.Parse(*focused_story_ptr);
  if (d.HasParseError()) {
    FTL_LOG(WARNING) << "Failed to parse JSON from context topic "
                     << kFocusedStoryTopic << ": " << *focused_story_ptr;
    return "";
  }

  if (d.IsNull())
    return "";

  if (!d.IsString()) {
    FTL_LOG(WARNING) << "JSON from context topic " << kFocusedStoryTopic
                     << " is not a string: " << *focused_story_ptr;
    return "";
  }

  return d.GetString();
}

void MaybeCopyTopic(const ContextRepository* repository,
                    const std::string& focused_story_id,
                    const std::string& topic,
                    std::map<std::string, std::string>* out) {
  // Copy the updated values from the focused story's namespace to the
  // focused alias namespace.
  std::string story_id;
  std::string rel_topic;
  if (!ParseStoryScopeTopic(topic, &story_id, &rel_topic) ||
      story_id != focused_story_id) {
    return;
  }

  const std::string* value = repository->Get(topic);
  const std::string focused_scope_topic = MakeFocusedStoryScopeTopic(rel_topic);
  if (value == nullptr) {
    (*out)[focused_scope_topic] = kJsonNull;
  } else {
    (*out)[focused_scope_topic] = *value;
  }
}

}  // namespace

FocusedStoryCoprocessor::FocusedStoryCoprocessor() = default;

FocusedStoryCoprocessor::~FocusedStoryCoprocessor() = default;

void FocusedStoryCoprocessor::ProcessTopicUpdate(
    const ContextRepository* repository,
    const std::set<std::string>& topics_updated,
    std::map<std::string, std::string>* out) {
  // Either:
  // a) The focused story has changed, in which case we need to copy
  //    everything over, and remove what's there already, or
  // b) Only certain values in the current story have changed, and
  //    we need to copy those values over.
  const bool focused_story_changed =
      topics_updated.count(kFocusedStoryTopic) > 0;
  const std::string focused_story_id = GetFocusedStoryId(repository);

  if (focused_story_changed) {  // (a)
    // Step 1: remove all existing values.
    std::vector<std::string> topics;
    repository->GetAllTopicsWithPrefix(MakeFocusedStoryScopeTopic("/"),
                                       &topics);
    for (const auto& topic : topics) {
      (*out)[topic] = kJsonNull;
    }

    // Step 2: copy in all new values, if relevant
    if (!focused_story_id.empty()) {
      topics.clear();
      repository->GetAllTopicsWithPrefix(
          MakeStoryScopeTopic(focused_story_id, "/"), &topics);

      for (const auto& topic : topics) {
        MaybeCopyTopic(repository, focused_story_id, topic, out);
      }
    }
  } else {  // (b)
    // Break out early if there isn't actually a focused story.
    if (focused_story_id.empty()) {
      return;
    }

    for (const auto& topic : topics_updated) {
      MaybeCopyTopic(repository, focused_story_id, topic, out);
    }
  }
}

}  // namespace maxwell
