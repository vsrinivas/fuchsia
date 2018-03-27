// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONTEXT_CPP_CONTEXT_METADATA_BUILDER_H_
#define LIB_CONTEXT_CPP_CONTEXT_METADATA_BUILDER_H_

#include <fuchsia/cpp/modular.h>

namespace maxwell {

class ContextMetadataBuilder {
 public:
  ContextMetadataBuilder();
  explicit ContextMetadataBuilder(modular::ContextMetadata initial_value);

  ContextMetadataBuilder& SetStoryId(const fidl::StringPtr& story_id);
  ContextMetadataBuilder& SetStoryFocused(bool focused);

  ContextMetadataBuilder& SetModuleUrl(const fidl::StringPtr& url);
  ContextMetadataBuilder& SetModulePath(const fidl::VectorPtr<fidl::StringPtr>& path);

  ContextMetadataBuilder& SetEntityTopic(const fidl::StringPtr& topic);
  ContextMetadataBuilder& AddEntityType(const fidl::StringPtr& type);
  ContextMetadataBuilder& SetEntityTypes(
      const fidl::VectorPtr<fidl::StringPtr>& types);

  ContextMetadataBuilder& SetLinkPath(
      const fidl::VectorPtr<fidl::StringPtr>& module_path,
      const fidl::StringPtr& name);

  // Build() can be called only once, as it moves |m_|.
  modular::ContextMetadata Build();

 private:
  modular::StoryMetadataPtr& StoryMetadata();
  modular::ModuleMetadataPtr& ModuleMetadata();
  modular::EntityMetadataPtr& EntityMetadata();
  modular::LinkMetadataPtr& LinkMetadata();

  modular::ContextMetadata m_;
};

}  // namespace maxwell

#endif  // LIB_CONTEXT_CPP_CONTEXT_METADATA_BUILDER_H_
