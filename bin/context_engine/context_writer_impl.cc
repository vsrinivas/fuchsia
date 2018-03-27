// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/context/cpp/formatting.h"
#include "lib/entity/cpp/json.h"
#include <fuchsia/cpp/modular.h>
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/context_engine/context_writer_impl.h"
#include "peridot/bin/context_engine/debug.h"
#include "rapidjson/document.h"

namespace maxwell {

ContextWriterImpl::ContextWriterImpl(
    const ComponentScopePtr& client_info,
    ContextRepository* const repository,
    modular::EntityResolver* const entity_resolver,
    f1dl::InterfaceRequest<ContextWriter> request)
    : binding_(this, std::move(request)),
      repository_(repository),
      entity_resolver_(entity_resolver),
      weak_factory_(this) {
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

f1dl::VectorPtr<f1dl::StringPtr> Deprecated_GetTypesFromJsonEntity(
    const f1dl::StringPtr& content) {
  // If the content has the @type attribute, take its contents and populate the
  // EntityMetadata appropriately, overriding whatever is there.
  std::vector<std::string> types;
  if (!modular::ExtractEntityTypesFromJson(content, &types)) {
    FXL_LOG(WARNING) << "Invalid entity metadata in JSON value: " << content;
    return {};
  }
  if (types.empty())
    return {};

  return f1dl::VectorPtr<f1dl::StringPtr>::From(types);
}

void MaybeFillEntityTypeMetadata(const f1dl::VectorPtr<f1dl::StringPtr>& types,
                                 ContextValuePtr* value_ptr) {
  auto& value = *value_ptr;
  if (value->type != ContextValueType::ENTITY || !types)
    return;

  if (!value->meta) {
    value->meta = ContextMetadata::New();
  }
  if (!value->meta->entity) {
    value->meta->entity = EntityMetadata::New();
  }
  value->meta->entity->type = types.Clone();
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
    f1dl::InterfaceRequest<ContextValueWriter> request,
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
  auto it = std::remove_if(
      value_writer_storage_.begin(), value_writer_storage_.end(),
      [ptr](const std::unique_ptr<ContextValueWriterImpl>& u_ptr) {
        return u_ptr.get() == ptr;
      });
  value_writer_storage_.erase(it, value_writer_storage_.end());
}

void ContextWriterImpl::WriteEntityTopic(const f1dl::StringPtr& topic,
                                         const f1dl::StringPtr& value) {
  auto activity = repository_->debug()->RegisterOngoingActivity();

  if (!value) {
    // Remove this value.
    auto it = topic_value_ids_.find(topic);
    if (it != topic_value_ids_.end()) {
      repository_->Remove(it->second);
    }
    return;
  }

  GetEntityTypesFromEntityReference(
      value, [this, activity, topic,
              value](const f1dl::VectorPtr<f1dl::StringPtr>& types) {
        auto value_ptr = ContextValue::New();
        value_ptr->type = ContextValueType::ENTITY;
        value_ptr->content = value;
        value_ptr->meta = ContextMetadata::New();
        value_ptr->meta->entity = EntityMetadata::New();
        value_ptr->meta->entity->topic = topic;
        value_ptr->meta->entity->type = types.Clone();

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
      });
}

void ContextWriterImpl::GetEntityTypesFromEntityReference(
    const f1dl::StringPtr& reference,
    std::function<void(const f1dl::VectorPtr<f1dl::StringPtr>&)> done) {
  auto activity = repository_->debug()->RegisterOngoingActivity();

  // TODO(thatguy): This function could be re-used in multiple places. Move it
  // somewhere other places can reach it.
  modular::EntityPtr entity;
  entity_resolver_->ResolveEntity(reference, entity.NewRequest());

  auto fallback = fxl::MakeAutoCall([done, reference] {
    // The contents of the Entity value could be a deprecated JSON Entity, not
    // an Entity reference.
    done(Deprecated_GetTypesFromJsonEntity(reference));
  });

  entity->GetTypes(fxl::MakeCopyable(
      [this, activity, id = entities_.GetId(&entity), done = std::move(done),
       fallback = std::move(fallback)](
          const f1dl::VectorPtr<f1dl::StringPtr>& types) mutable {
        done(types);
        fallback.cancel();
        entities_.erase(id);
      }));

  entities_.emplace(std::move(entity));
}

ContextValueWriterImpl::ContextValueWriterImpl(
    ContextWriterImpl* writer,
    const ContextRepository::Id& parent_id,
    ContextValueType type,
    f1dl::InterfaceRequest<ContextValueWriter> request)
    : binding_(this, std::move(request)),
      writer_(writer),
      parent_id_(parent_id),
      type_(type),
      weak_factory_(this) {
  binding_.set_error_handler(
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
    f1dl::InterfaceRequest<ContextValueWriter> request,
    ContextValueType type) {
  // We can't create a child value until this value has an ID.
  value_id_.OnValue(
      fxl::MakeCopyable([this, request = std::move(request),
                         type](const ContextRepository::Id& value_id) mutable {
        auto ptr = new ContextValueWriterImpl(writer_, value_id, type,
                                              std::move(request));
        writer_->AddContextValueWriter(ptr);
      }));
}

void ContextValueWriterImpl::Set(const f1dl::StringPtr& content,
                                 ContextMetadataPtr metadata) {
  auto activity = writer_->repository()->debug()->RegisterOngoingActivity();

  auto done_getting_types =
      [weak_this = weak_factory_.GetWeakPtr(), activity, content,
       metadata = std::move(metadata)](
          const f1dl::VectorPtr<f1dl::StringPtr>& entity_types) mutable {
        if (!weak_this)
          return;

        if (!weak_this->value_id_) {
          // We're creating this value for the first time.
          auto value = ContextValue::New();
          value->type = weak_this->type_;
          value->content = content;
          value->meta = std::move(metadata);
          MaybeFillEntityTypeMetadata(entity_types, &value);

          if (weak_this->parent_id_.empty()) {
            weak_this->value_id_ =
                weak_this->writer_->repository()->Add(std::move(value));
          } else {
            weak_this->value_id_ = weak_this->writer_->repository()->Add(
                weak_this->parent_id_, std::move(value));
          }
        } else {
          if (!weak_this->writer_->repository()->Contains(
                  weak_this->value_id_.get())) {
            FXL_LOG(FATAL) << "Trying to update non-existent context value ("
                           << weak_this->value_id_.get()
                           << "). New content: " << content
                           << ", new metadata: " << metadata;
          }

          auto value =
              weak_this->writer_->repository()->Get(weak_this->value_id_.get());
          if (content) {
            value->content = content;
          }
          if (metadata) {
            value->meta = metadata.Clone();
          }
          MaybeFillEntityTypeMetadata(entity_types, &value);
          weak_this->writer_->repository()->Update(weak_this->value_id_.get(),
                                                   std::move(value));
        }
      };

  if (type_ != ContextValueType::ENTITY || !content) {
    // Avoid an extra round-trip to EntityResolver that won't get us anything.
    done_getting_types({});
  } else {
    writer_->GetEntityTypesFromEntityReference(
        content, fxl::MakeCopyable(std::move(done_getting_types)));
  }
}

}  // namespace maxwell
