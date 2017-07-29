// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_publisher_impl.h"

#include "apps/maxwell/src/context_engine/context_repository.h"
#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "rapidjson/document.h"

namespace maxwell {

namespace {

const char kEntityTypeProperty[] = "@type";

bool ParseAndValidateJson(const fidl::String& value, rapidjson::Document* doc) {
  if (value.is_null())
    return false;
  doc->Parse(value);
  return !doc->HasParseError();
}

}  // namespace

ContextPublisherImpl::ContextPublisherImpl(ComponentScopePtr scope,
                                           ContextRepository* repository)
    : scope_(std::move(scope)),
      metadata_(ContextMetadata::New()),
      repository_(repository) {
  if (scope_->is_module_scope()) {
    metadata_->story = StoryMetadata::New();
    metadata_->story->id = scope_->get_module_scope()->story_id;
    metadata_->mod = ModuleMetadata::New();
    metadata_->mod->url = scope_->get_module_scope()->url;
    metadata_->mod->path = scope_->get_module_scope()->module_path.Clone();
  }
}
ContextPublisherImpl::~ContextPublisherImpl() = default;

void ContextPublisherImpl::Publish(const fidl::String& topic,
                                   const fidl::String& json_data) {
  rapidjson::Document doc;
  if (!ParseAndValidateJson(json_data, &doc)) {
    FTL_LOG(WARNING) << "Invalid JSON for " << topic << ": " << json_data;
    return;
  }

  // Rewrite the topic to be within the scope specified at construction time.
  std::string local_topic = topic;
  if (scope_->is_module_scope()) {
    // If a Mod is publishing this, prefix its topic string with "explicit", to
    // indicate that the Mod is explicitly publishing this value.
    local_topic = ConcatTopic("explicit", topic);
  }
  const auto scoped_topic = ScopeAndTopicToString(scope_, local_topic);

  ContextValue value;
  value.json = json_data;
  value.meta = metadata_.Clone();
  value.meta->entity = EntityMetadata::New();
  /// TODO(thatguy): This should be set to |topic|, once clients have been
  //updated to query based on metadata values.
  value.meta->entity->topic = scoped_topic;
  if (doc.IsObject() && doc.HasMember(kEntityTypeProperty)) {
    const auto& types = doc[kEntityTypeProperty];
    if (types.IsString()) {
      value.meta->entity->type = fidl::Array<fidl::String>::New(1);
      value.meta->entity->type[0] = types.GetString();
    } else if (types.IsArray()) {
      value.meta->entity->type = fidl::Array<fidl::String>::New(types.Size());
      for (uint32_t i = 0; i < types.Size(); ++i) {
        if (!types[i].IsString()) continue;
        value.meta->entity->type[i] = types[i].GetString();
      }
    }
  }

  repository_->Set(scoped_topic, std::move(value));
}

}  // namespace maxwell
