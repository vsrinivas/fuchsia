// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/context_writer_impl.h"
#include "lib/context/cpp/formatting.h"
#include "rapidjson/document.h"

namespace maxwell {

ContextWriterImpl::ContextWriterImpl(
    const ComponentScopePtr& client_info,
    ContextRepository* repository,
    fidl::InterfaceRequest<ContextWriter> request)
    : binding_(this, std::move(request)), repository_(repository) {
  FXL_DCHECK(repository != nullptr);

  // Set up a query to the repository to get our parent id.
  if (client_info->is_module_scope()) {
    auto selector = ContextSelector::New();
    selector->type = ContextValueType::MODULE;
    selector->meta = ContextMetadata::New();
    selector->meta->story = StoryMetadata::New();
    selector->meta->story->id = client_info->get_module_scope()->story_id;
    selector->meta->mod = ModuleMetadata::New();
    selector->meta->mod->path =
        client_info->get_module_scope()->module_path.Clone();

    parent_value_selector_ = std::move(selector);
  }
}

ContextWriterImpl::~ContextWriterImpl() {}

namespace {

const char kEntityTypeProperty[] = "@type";

void MaybeFillEntityMetadata(ContextValuePtr* const value_ptr) {
  ContextValuePtr& value = *value_ptr;  // For sanity.
  if (value->type != ContextValueType::ENTITY)
    return;

  // If the content has the @type attribute, take its contents and populate the
  // EntityMetadata appropriately, overriding whatever is there.
  rapidjson::Document doc;
  doc.Parse(value->content);
  if (doc.HasParseError()) {
    FXL_LOG(WARNING) << "Invalid JSON in value: " << value;
    return;
  }
  fidl::Array<fidl::String> new_types;
  if (doc.IsObject() && doc.HasMember(kEntityTypeProperty)) {
    const auto& types = doc[kEntityTypeProperty];
    if (types.IsString()) {
      new_types = fidl::Array<fidl::String>::New(1);
      new_types[0] = types.GetString();
    } else if (types.IsArray()) {
      new_types = fidl::Array<fidl::String>::New(types.Size());
      for (uint32_t i = 0; i < types.Size(); ++i) {
        if (!types[i].IsString())
          continue;
        new_types[i] = types[i].GetString();
      }
    }
  }

  if (!value->meta) {
    value->meta = ContextMetadata::New();
  }
  if (!value->meta->entity) {
    value->meta->entity = EntityMetadata::New();
  }
  value->meta->entity->type = std::move(new_types);
}

bool MaybeFindParentValueId(ContextRepository* repository,
                            const ContextSelectorPtr& selector,
                            ContextRepository::Id* out) {
  if (!selector)
    return false;
  // There is technically a race condition here, since on construction, we
  // are given a ComponentScope, which contains some metadata to find a value
  // in the context engine. It is the responsibility of the story_info
  // acquierer to actually create that value, so we query here at
  // AddValue()-time because it makes it less likely to hit the race
  // condition.
  //
  // This is only exercised when a Module publishes context explicitly,
  // something that we plan to disallow once Links speak in Entities, as then
  // Modules that wish to store context can simply write Entities into a new
  // link.
  auto ids = repository->Select(selector.Clone());
  if (ids.size() == 1) {
    *out = *ids.begin();
    return true;
  }
  return false;
}

}  // namespace

void ContextWriterImpl::AddValue(ContextValuePtr value,
                                 const AddValueCallback& done) {
  MaybeFillEntityMetadata(&value);
  if (parent_value_selector_) {
    ContextRepository::Id parent_id;
    if (MaybeFindParentValueId(repository_, parent_value_selector_,
                               &parent_id)) {
      done(repository_->Add(parent_id, std::move(value)));
      return;
    }
  }
  done(repository_->Add(std::move(value)));
}

void ContextWriterImpl::AddChildValue(const fidl::String& parent_id,
                                      ContextValuePtr value,
                                      const AddChildValueCallback& done) {
  MaybeFillEntityMetadata(&value);
  // TODO(thatguy): Error handling when |parent_id| no longer exists.
  auto id = repository_->Add(parent_id, std::move(value));
  done(id);
}

void ContextWriterImpl::Update(const fidl::String& id,
                               ContextValuePtr new_value) {
  if (!repository_->Contains(id)) {
    FXL_LOG(WARNING)
        << "Trying to update content on non-existent context value (" << id
        << "). New value: " << new_value;
  }
  repository_->Update(id, std::move(new_value));
}

void ContextWriterImpl::UpdateContent(const fidl::String& id,
                                      const fidl::String& content) {
  auto value = repository_->Get(id);
  if (!value) {
    FXL_LOG(WARNING)
        << "Trying to update content on non-existent context value (" << id
        << "). Content: " << content;
  }

  value->content = content;
  repository_->Update(id, std::move(value));
}

void ContextWriterImpl::UpdateMetadata(const fidl::String& id,
                                       ContextMetadataPtr metadata) {
  auto value = repository_->Get(id);
  if (!value) {
    FXL_LOG(WARNING)
        << "Trying to update metadata on non-existent context value (" << id
        << "). Metadata: " << metadata;
  }

  value->meta = std::move(metadata);
  repository_->Update(id, std::move(value));
}

void ContextWriterImpl::Remove(const fidl::String& id) {
  repository_->Remove(id);
}

void ContextWriterImpl::WriteEntityTopic(const fidl::String& topic,
                                         const fidl::String& value) {
  if (!value) {
    // Remove this value.
    auto it = topic_value_ids_.find(topic);
    if (it != topic_value_ids_.end()) {
      repository_->Remove(it->second);
    }
    return;
  }

  auto value_ptr = ContextValue::New();
  value_ptr->type = ContextValueType::ENTITY;
  value_ptr->content = value;
  value_ptr->meta = ContextMetadata::New();
  value_ptr->meta->entity = EntityMetadata::New();
  value_ptr->meta->entity->topic = topic;
  MaybeFillEntityMetadata(&value_ptr);

  auto it = topic_value_ids_.find(topic);
  if (it == topic_value_ids_.end()) {
    ContextRepository::Id parent_id;
    ContextRepository::Id id;
    if (MaybeFindParentValueId(repository_, parent_value_selector_,
                               &parent_id)) {
      id = repository_->Add(parent_id, std::move(value_ptr));
    } else {
      id = repository_->Add(std::move(value_ptr));
    }
    topic_value_ids_[topic] = id;
  } else {
    repository_->Update(it->second, std::move(value_ptr));
  }
}

}  // namespace maxwell
