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

ContextMetadataPtr ContextMetadataBuilder::Build() {
  return std::move(m_);
}

StoryMetadataPtr& ContextMetadataBuilder::StoryMetadata() {
  if (!m_)
    m_ = ContextMetadata::New();
  if (!m_->story) {
    m_->story = StoryMetadata::New();
  }
  return m_->story;
}

ModuleMetadataPtr& ContextMetadataBuilder::ModuleMetadata() {
  if (!m_)
    m_ = ContextMetadata::New();
  if (!m_->mod) {
    m_->mod = ModuleMetadata::New();
  }
  return m_->mod;
}

EntityMetadataPtr& ContextMetadataBuilder::EntityMetadata() {
  if (!m_)
    m_ = ContextMetadata::New();
  if (!m_->entity) {
    m_->entity = EntityMetadata::New();
  }
  return m_->entity;
}

}  // namespace maxwell
