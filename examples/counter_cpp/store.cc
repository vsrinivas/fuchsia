// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/examples/counter_cpp/store.h"

#include <iterator>
#include <utility>

#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/story/link.fidl.h"
#include "lib/fidl/cpp/bindings/map.h"

using fidl::InterfaceHandle;
using fidl::String;

using modular::Link;
using modular::LinkWatcher;

namespace modular_example {

Counter::Counter(const rapidjson::Value& /*name*/,
                 const rapidjson::Value& value) {
  // Updates may be incremental, so don't assume that all fields are present.
  auto itr = value.FindMember(kSenderKey);
  if (itr != value.MemberEnd()) {
    FTL_CHECK(itr->value.IsString());
    sender = itr->value.GetString();
  }

  itr = value.FindMember(kCounterKey);
  if (itr != value.MemberEnd()) {
    FTL_CHECK(itr->value.IsInt());
    counter = itr->value.GetInt();
  }

  // For the last iteration, test that Module2 removes the sender.
  if (counter <= 10) {
    FTL_CHECK(!sender.empty());
  } else {
    FTL_CHECK(sender.empty());
  }

  FTL_CHECK(is_valid());
}

rapidjson::Document Counter::ToDocument(const std::string& module_name) {
  rapidjson::Document counter_doc;
  auto& allocator1 = counter_doc.GetAllocator();
  counter_doc.SetObject();
  counter_doc.AddMember(kCounterKey, counter, allocator1);
  if (counter >= 11) {
    // TODO(jimbe) remove the sender property to prove that property removal
    // works. This requires calling Erase(), but this code isn't structured
    // to work that way right now because it just returns a json string.
    // A related open question is how we should implement deletion based on
    // the JSON string we return. One proposal is for UpdateObject() to also
    // take a list of keys to delete.
    counter_doc.AddMember(kSenderKey, "", allocator1);
  } else {
    counter_doc.AddMember(kSenderKey, module_name, allocator1);
  }

  return counter_doc;
}

Store::Store(std::string module_name)
    : module_name_(std::move(module_name)), watcher_binding_(this) {}

void Store::Initialize(InterfaceHandle<Link> link) {
  link_.Bind(std::move(link));

  InterfaceHandle<LinkWatcher> watcher;
  watcher_binding_.Bind(&watcher);
  link_->Watch(std::move(watcher));
}

void Store::AddCallback(Callback c) {
  callbacks_.emplace_back(std::move(c));
}

void Store::Stop() {
  watcher_binding_.Close();
  link_.reset();
}

void Store::Notify(const fidl::String& json) {
  FTL_LOG(INFO) << "Store::Notify() " << module_name_;
  ApplyLinkData(json.get());
}

// Process an update from the Link and write it to our local copy.
// The update is ignored if:
//   - it's missing the desired document.
//   - the data in the update is stale (can happen on rehydrate).
void Store::ApplyLinkData(const std::string& json) {
  rapidjson::Document doc;
  doc.Parse(json);
  FTL_CHECK(!doc.HasParseError());
  FTL_LOG(INFO) << "Store::ApplyLinkData() " << module_name_ << " "
                << modular::JsonValueToPrettyString(doc);
  if (doc.IsNull()) {
    // Received an empty update, which means we are starting a new story.
    // Don't do anything now, the recipe will gives us the initial data.
    return;
  }

  rapidjson::Pointer ptr(modular_example::kJsonPath);
  rapidjson::Value* const value = ptr.Get(doc);
  FTL_CHECK(value != nullptr);

  auto itr = value->GetObject().MemberBegin();
  FTL_CHECK(itr != value->GetObject().MemberEnd());

  modular_example::Counter new_counter(itr->name, itr->value);

  // Redundant update, ignore it.
  if (new_counter.counter <= counter.counter) {
    return;
  }

  // If we sent it, then we are getting a message from a restored session.
  // We don't know if it was ever actually delivered, so send it again.
  if (new_counter.sender == module_name_) {
    MarkDirty();
  }
  counter = std::move(new_counter);
  ModelChanged();
}

void Store::ModelChanged() {
  for (auto& c : callbacks_) {
    c();
  }
  SendIfDirty();
}

void Store::SendIfDirty() {
  FTL_LOG(INFO) << "Store::SendIfDirty() " << module_name_;

  if (link_ && dirty_) {
    rapidjson::Document doc = counter.ToDocument(module_name_);

    FTL_CHECK(link_.get() != nullptr);
    FTL_CHECK(doc.IsObject());

    std::vector<std::string> segments{modular_example::kJsonSegment,
                                      modular_example::kDocId};
    link_->UpdateObject(fidl::Array<fidl::String>::From(segments),
                        modular::JsonValueToString(doc));
    dirty_ = false;
  }
}
}  // namespace modular_example
