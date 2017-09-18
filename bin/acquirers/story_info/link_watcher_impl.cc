// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/acquirers/story_info/link_watcher_impl.h"

#include <sstream>

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/src/acquirers/story_info/story_watcher_impl.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "garnet/public/lib/fxl/functional/make_copyable.h"

namespace maxwell {

namespace {

constexpr char kContextProperty[] = "@context";
constexpr char kSourceProperty[] = "@source";

struct Context {
  fidl::String topic;
};

struct Source {
  fidl::String story_id;
  fidl::Array<fidl::String> module_path;
  fidl::String link_name;
};

void XdrContext(modular::XdrContext* const xdr, Context* const data) {
  xdr->Field("topic", &data->topic);
}

void XdrSource(modular::XdrContext* const xdr, Source* const data) {
  xdr->Field("story_id", &data->story_id);
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link_name", &data->link_name);
}

std::string MakeLinkTopic(const fidl::String& base_topic) {
  std::stringstream s;
  s << "link/" << base_topic;
  return s.str();
}

}  // namespace

LinkWatcherImpl::LinkWatcherImpl(
    StoryWatcherImpl* const owner,
    modular::StoryController* const story_controller,
    ContextWriter* const writer,
    const std::string& story_id,
    const fidl::String& parent_value_id,
    const modular::LinkPathPtr& link_path)
    : owner_(owner),
      story_controller_(story_controller),
      writer_(writer),
      story_id_(story_id),
      parent_value_id_(parent_value_id),
      link_path_(link_path->Clone()),
      link_watcher_binding_(this) {
  modular::LinkPtr link;
  story_controller_->GetLink(link_path_->module_path.Clone(),
                             link_path_->link_name, link.NewRequest());

  link->Watch(link_watcher_binding_.NewBinding());

  // If the link becomes inactive, we stop watching it. It might still receive
  // updates from other devices, but nothing can tell us as it isn't kept in
  // memory on the current device.
  //
  // The Link itself is not kept here, because otherwise it never becomes
  // inactive (i.e. loses all its Link connections).
  link_watcher_binding_.set_connection_error_handler(
      [this] { owner_->DropLink(MakeLinkKey(link_path_)); });
}

LinkWatcherImpl::~LinkWatcherImpl() = default;

void LinkWatcherImpl::Notify(const fidl::String& json) {
  ProcessContext(json);
}

void LinkWatcherImpl::ProcessContext(const fidl::String& value) {
  modular::JsonDoc doc;
  doc.Parse(value);
  FXL_CHECK(!doc.HasParseError());

  if (!doc.IsObject()) {
    return;
  }

  auto i = doc.FindMember(kContextProperty);
  if (i == doc.MemberEnd()) {
    return;
  }

  modular::JsonDoc context_doc;
  context_doc.CopyFrom(i->value, context_doc.GetAllocator());
  doc.RemoveMember(i);

  if (!context_doc.IsObject()) {
    return;
  }

  Context context;
  if (!modular::XdrRead(&context_doc, &context, XdrContext)) {
    return;
  }

  Source source;
  source.story_id = story_id_;
  source.module_path = link_path_->module_path.Clone();
  source.link_name = link_path_->link_name;

  modular::JsonDoc source_doc;
  modular::XdrWrite(&source_doc, &source, XdrSource);
  doc.AddMember(kSourceProperty, source_doc, doc.GetAllocator());

  auto context_value = ContextValue::New();
  std::string json = modular::JsonValueToString(doc);
  context_value->content = json;
  context_value->type = ContextValueType::ENTITY;
  context_value->meta = ContextMetadata::New();
  context_value->meta->entity = EntityMetadata::New();
  context_value->meta->entity->topic = MakeLinkTopic(context.topic);

  FXL_LOG(INFO) << "Publishing context: " << context_value << std::endl
                << "Original link value: " << value << std::endl
                << "Parent context value ID: " << parent_value_id_;
  auto it = value_ids_.find(context.topic);
  // TODO(thatguy): This pattern of wanting to have a single Context value
  // and update its state seems to come up. Might be worth creating a
  // ScopedContextValue class that wraps this logic right here and exposes
  // a simple Set() operation.
  if (it == value_ids_.end()) {
    const fidl::String topic = context.topic;
    value_ids_.emplace(topic, FutureValue<fidl::String>());
    writer_->AddChildValue(parent_value_id_, std::move(context_value),
                           [this, topic](const fidl::String& value_id) {
                             value_ids_[topic] = value_id;
                           });
  } else {
    it->second.OnValue(fxl::MakeCopyable([ this, value = std::move(context_value) ](
        const fidl::String& value_id) mutable {
      writer_->Update(value_id, std::move(value));
    }));
  }
}

}  // namespace maxwell
