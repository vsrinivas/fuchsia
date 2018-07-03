// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/context/cpp/context_metadata_builder.h>

#include <lib/fidl/cpp/clone.h>

namespace maxwell {

ContextMetadataBuilder::ContextMetadataBuilder() {}
ContextMetadataBuilder::ContextMetadataBuilder(
    fuchsia::modular::ContextMetadata initial_value)
    : m_(std::move(initial_value)) {}

ContextMetadataBuilder& ContextMetadataBuilder::SetStoryId(
    const fidl::StringPtr& story_id) {
  StoryMetadata()->id = story_id;
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::SetStoryFocused(bool focused) {
  auto& story_meta = StoryMetadata();
  story_meta->focused = fuchsia::modular::FocusedState::New();
  story_meta->focused->state =
      focused ? fuchsia::modular::FocusedStateState::FOCUSED
              : fuchsia::modular::FocusedStateState::NOT_FOCUSED;
  return *this;
}

ContextMetadataBuilder& ContextMetadataBuilder::SetModuleUrl(
    const fidl::StringPtr& url) {
  ModuleMetadata()->url = url;
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::SetModulePath(
    const fidl::VectorPtr<fidl::StringPtr>& path) {
  fidl::Clone(path, &ModuleMetadata()->path);
  return *this;
}

ContextMetadataBuilder& ContextMetadataBuilder::SetEntityTopic(
    const fidl::StringPtr& topic) {
  EntityMetadata()->topic = topic;
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::AddEntityType(
    const fidl::StringPtr& type) {
  EntityMetadata()->type.push_back(type);
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::SetEntityTypes(
    const fidl::VectorPtr<fidl::StringPtr>& types) {
  fidl::Clone(types, &EntityMetadata()->type);
  return *this;
}
ContextMetadataBuilder& ContextMetadataBuilder::SetLinkPath(
    const fidl::VectorPtr<fidl::StringPtr>& module_path,
    const fidl::StringPtr& name) {
  fidl::Clone(module_path, &LinkMetadata()->module_path);
  LinkMetadata()->name = name;
  return *this;
}

fuchsia::modular::ContextMetadata ContextMetadataBuilder::Build() {
  return std::move(m_);
}

fuchsia::modular::ContextMetadataPtr ContextMetadataBuilder::BuildPtr() {
  auto meta = fuchsia::modular::ContextMetadata::New();
  *meta.get() = std::move(m_);
  return meta;
}

#define ENSURE_MEMBER(field, class_name)            \
  if (!m_.field) {                                  \
    m_.field = fuchsia::modular::class_name::New(); \
  }                                                 \
  return m_.field;

fuchsia::modular::StoryMetadataPtr& ContextMetadataBuilder::StoryMetadata() {
  ENSURE_MEMBER(story, StoryMetadata);
}

fuchsia::modular::ModuleMetadataPtr& ContextMetadataBuilder::ModuleMetadata() {
  ENSURE_MEMBER(mod, ModuleMetadata);
}

fuchsia::modular::EntityMetadataPtr& ContextMetadataBuilder::EntityMetadata() {
  ENSURE_MEMBER(entity, EntityMetadata);
}

fuchsia::modular::LinkMetadataPtr& ContextMetadataBuilder::LinkMetadata() {
  ENSURE_MEMBER(link, LinkMetadata);
}

}  // namespace maxwell
