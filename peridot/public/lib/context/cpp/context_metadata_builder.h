// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONTEXT_CPP_CONTEXT_METADATA_BUILDER_H_
#define LIB_CONTEXT_CPP_CONTEXT_METADATA_BUILDER_H_

#include <fuchsia/modular/cpp/fidl.h>

namespace maxwell {

class ContextMetadataBuilder {
 public:
  ContextMetadataBuilder();
  explicit ContextMetadataBuilder(
      fuchsia::modular::ContextMetadata initial_value);

  ContextMetadataBuilder& SetStoryId(const fidl::StringPtr& story_id);
  ContextMetadataBuilder& SetStoryFocused(bool focused);

  ContextMetadataBuilder& SetModuleUrl(const fidl::StringPtr& url);
  ContextMetadataBuilder& SetModulePath(const std::vector<std::string>& path);
  ContextMetadataBuilder& SetModuleFocused(bool focused);

  ContextMetadataBuilder& SetEntityTopic(const fidl::StringPtr& topic);
  ContextMetadataBuilder& AddEntityType(const fidl::StringPtr& type);
  ContextMetadataBuilder& SetEntityTypes(const std::vector<std::string>& types);

  ContextMetadataBuilder& SetLinkPath(
      const std::vector<std::string>& module_path, const std::string& name);

  // Build() or BuildPtr() can be called only once, as they move |m_|.
  fuchsia::modular::ContextMetadata Build();
  fuchsia::modular::ContextMetadataPtr BuildPtr();

 private:
  fuchsia::modular::StoryMetadataPtr& StoryMetadata();
  fuchsia::modular::ModuleMetadataPtr& ModuleMetadata();
  fuchsia::modular::EntityMetadataPtr& EntityMetadata();
  fuchsia::modular::LinkMetadataPtr& LinkMetadata();

  fuchsia::modular::ContextMetadata m_;
};

}  // namespace maxwell

#endif  // LIB_CONTEXT_CPP_CONTEXT_METADATA_BUILDER_H_
