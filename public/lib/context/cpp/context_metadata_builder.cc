// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/context_metadata_builder.h"

namespace maxwell {

ContextMetadataBuilder::ContextMetadataBuilder() {}
ContextMetadataBuilder::ContextMetadataBuilder(ContextMetadataPtr initial_value)
    : m_(std::move(initial_value)) {}

ContextMetadataBuilder& ContextMetadataBuilder::SetStoryId(
    const fidl::String& story_id) {
  StoryMetadata()->id = story_id;
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::SetStoryFocused(bool focused) {
  auto& story_meta = StoryMetadata();
  story_meta->focused = FocusedState::New();
  story_meta->focused->state =
      focused ? FocusedState::State::FOCUSED : FocusedState::State::NOT_FOCUSED;
  return *this;
}

ContextMetadataBuilder& ContextMetadataBuilder::SetModuleUrl(
    const fidl::String& url) {
  ModuleMetadata()->url = url;
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::SetModulePath(
    const fidl::Array<fidl::String>& path) {
  ModuleMetadata()->path = path.Clone();
  return *this;
}

ContextMetadataBuilder& ContextMetadataBuilder::SetEntityTopic(
    const fidl::String& topic) {
  EntityMetadata()->topic = topic;
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::AddEntityType(
    const fidl::String& type) {
  EntityMetadata()->type.push_back(type);
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::SetEntityTypes(
    const fidl::Array<fidl::String>& types) {
  EntityMetadata()->type = types.Clone();
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::SetLinkPath(
    const fidl::Array<fidl::String>& module_path,
    const fidl::String& name) {
  LinkMetadata()->module_path = module_path.Clone();
  LinkMetadata()->name = name;
  return *this;
}

ContextMetadataPtr ContextMetadataBuilder::Build() {
  return std::move(m_);
}

#define ENSURE_MEMBER(field, class_name) \
  if (!m_)                               \
    m_ = ContextMetadata::New();         \
  if (!m_->field) {                      \
    m_->field = class_name::New();       \
  }                                      \
  return m_->field;

StoryMetadataPtr& ContextMetadataBuilder::StoryMetadata() {
  ENSURE_MEMBER(story, StoryMetadata);
}

ModuleMetadataPtr& ContextMetadataBuilder::ModuleMetadata() {
  ENSURE_MEMBER(mod, ModuleMetadata);
}

EntityMetadataPtr& ContextMetadataBuilder::EntityMetadata() {
  ENSURE_MEMBER(entity, EntityMetadata);
}

LinkMetadataPtr& ContextMetadataBuilder::LinkMetadata() {
  ENSURE_MEMBER(link, LinkMetadata);
}

}  // namespace maxwell
