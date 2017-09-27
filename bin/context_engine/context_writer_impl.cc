// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/context_writer_impl.h"
#include "lib/context/cpp/formatting.h"
#include "garnet/public/lib/fxl/functional/make_copyable.h"
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
  if (!new_types) {
    return;
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
  // acquierer to actually create that value, so we query at
  // CreateValue()-time because it makes it less likely to hit the race
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

void ContextWriterImpl::CreateValue(
    fidl::InterfaceRequest<ContextValueWriter> request,
    ContextValueType type) {
  ContextRepository::Id parent_id;
  // We ignore the return value - if it returns false |parent_id| will stay
  // default-initialized.
  MaybeFindParentValueId(repository_, parent_value_selector_, &parent_id);
  auto ptr =
      new ContextValueWriterImpl(this, parent_id, type, std::move(request));
  AddContextValueWriter(ptr);
}

void ContextWriterImpl::AddContextValueWriter(ContextValueWriterImpl* ptr) {
  value_writer_storage_.emplace_back(ptr);
}

void ContextWriterImpl::DestroyContextValueWriter(ContextValueWriterImpl* ptr) {
  std::remove_if(value_writer_storage_.begin(), value_writer_storage_.end(),
                 [ptr](const std::unique_ptr<ContextValueWriterImpl>& u_ptr) {
                   return u_ptr.get() == ptr;
                 });
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

ContextValueWriterImpl::ContextValueWriterImpl(
    ContextWriterImpl* writer,
    const ContextRepository::Id& parent_id,
    ContextValueType type,
    fidl::InterfaceRequest<ContextValueWriter> request)
    : binding_(this, std::move(request)),
      writer_(writer),
      parent_id_(parent_id),
      type_(type) {
  binding_.set_connection_error_handler(
      [this] { writer_->DestroyContextValueWriter(this); });
}

ContextValueWriterImpl::~ContextValueWriterImpl() {
  // It's possible we haven't actually created a value in |repository_| yet.
  if (value_id_) {
    // Remove the value.
    writer_->repository()->Remove(value_id_.get());
  }
}

void ContextValueWriterImpl::CreateChildValue(
    fidl::InterfaceRequest<ContextValueWriter> request,
    ContextValueType type) {
  // We can't create a child value until this value has an ID.
  value_id_.OnValue(fxl::MakeCopyable([
    this, request = std::move(request), type
  ](const ContextRepository::Id& value_id) mutable {
    auto ptr =
        new ContextValueWriterImpl(writer_, value_id, type, std::move(request));
    writer_->AddContextValueWriter(ptr);
  }));
}

void ContextValueWriterImpl::Set(const fidl::String& content,
                                 ContextMetadataPtr metadata) {
  if (!value_id_) {
    // We're creating this value for the first time.
    auto value = ContextValue::New();
    value->type = type_;
    value->content = content;
    value->meta = std::move(metadata);
    MaybeFillEntityMetadata(&value);

    if (parent_id_.empty()) {
      value_id_ = writer_->repository()->Add(std::move(value));
    } else {
      value_id_ = writer_->repository()->Add(parent_id_, std::move(value));
    }
  } else {
    if (!writer_->repository()->Contains(value_id_.get())) {
      FXL_LOG(FATAL) << "Trying to update non-existent context value ("
                     << value_id_.get() << "). New content: " << content
                     << ", new metadata: " << metadata;
    }

    auto value = writer_->repository()->Get(value_id_.get());
    if (content) {
      value->content = content;
    }
    if (metadata) {
      value->meta = std::move(metadata);
    }
    MaybeFillEntityMetadata(&value);
    writer_->repository()->Update(value_id_.get(), std::move(value));
  }
}

}  // namespace maxwell
