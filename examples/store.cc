// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include "apps/modular/examples/store.h"

#include "apps/modular/lib/document_editor/document_editor.h"
#include "apps/modular/services/story/link.fidl.h"
#include "lib/fidl/cpp/bindings/map.h"

using document_store::Document;
using document_store::Value;

using fidl::Array;
using fidl::Binding;
using fidl::InterfaceHandle;
using fidl::InterfacePtr;
using fidl::InterfaceRequest;
using fidl::Map;
using fidl::String;

using modular::DocumentEditor;
using modular::FidlDocMap;
using modular::Link;
using modular::LinkWatcher;
using modular::operator<<;

namespace modular_example {

Counter::Counter(DocumentEditor* editor) {
  Value* sender_value = editor->GetValue(kSenderLabel);
  Value* counter_value = editor->GetValue(kCounterLabel);

  // Updates may be incremental, so don't assume that all fields are present.
  if (sender_value)
    sender = std::move(sender_value->get_string_value());
  if (counter_value) {
    counter = counter_value->get_int_value();
  }

  // For the last iteration, test that Module2 removes the sender.
  if (counter <= 10)
    FTL_DCHECK(!sender.empty());
  else
    FTL_DCHECK(sender.empty());

  FTL_DCHECK(is_valid());
}

std::unique_ptr<DocumentEditor> Counter::ToDocument(const std::string& module_name) {
  auto editor = std::make_unique<DocumentEditor>(kDocId);
  editor->SetProperty(kCounterLabel, DocumentEditor::NewIntValue(counter))
      .SetProperty(kSenderLabel, DocumentEditor::NewStringValue(module_name));

  // For the last value, remove the sender property to prove that property
  // removal works.
  if (counter == 11) {
    editor->RemoveProperty(kSenderLabel);
  }

  return editor;
}
}  // namespace modular_example

namespace modular {

Store::Store(const std::string& module_name)
    : module_name_(module_name), watcher_binding_(this) {
  FTL_LOG(INFO) << "Store::Store" << module_name_;
}

void Store::Initialize(InterfaceHandle<Link> link) {
  link_.Bind(std::move(link));

  InterfaceHandle<LinkWatcher> watcher;
  watcher_binding_.Bind(&watcher);
  link_->Watch(std::move(watcher));

  watcher_binding_.set_connection_error_handler(
      []() { FTL_LOG(INFO) << "binding_.set_connection_error_handler "; });
}

void Store::AddCallback(Callback c) {
  callbacks_.emplace_back(std::move(c));
}

void Store::Stop() {
  watcher_binding_.Close();
  link_.reset();
}

// See comments on Module2Impl in example-module2.cc.
void Store::Notify(FidlDocMap docs) {
  FTL_LOG(INFO) << "Store::Notify() " << module_name_ << " " << (int64_t)this
                << docs;
  ApplyLinkData(std::move(docs));
}

// Process an update from the Link and write it to our local copy.
// The update is ignored if:
//   - it's missing the desired document.
//   - the data in the update is stale (can happen on rehydrate).
void Store::ApplyLinkData(FidlDocMap docs) {
  // There's only supposed to be one document.
  FTL_DCHECK(docs.size() <= 1);
  if (docs.size() == 0) {
    // Received an empty update, which means we are starting a new story.
    // Don't do anything now, the recipe will gives us the initial data.
    return;
  }

  DocumentEditor editor(docs.begin().GetValue().get());
  modular_example::Counter new_counter(&editor);

  // Redundant update, ignore it.
  if (new_counter.counter <= counter.counter) {
    FTL_LOG(INFO) << "Module1Impl::ApplyLinkData() ************ IGNORE "
                  << module_name_;
    return;
  }

  // If we sent it, then we are getting a message from a restored session.
  // We don't know if it was ever actually delivered, so send it again.
  if (new_counter.sender == module_name_)
    MarkDirty();
  counter = std::move(new_counter);
  ModelChanged();
}

void Store::ModelChanged() {
  for (auto& c : callbacks_)
    c();
  SendIfDirty();
}

void Store::SendIfDirty() {
  if (link_ && dirty_) {
    FidlDocMap docs;
    auto editor = counter.ToDocument(module_name_);
    editor->Insert(&docs);

    FTL_DCHECK(link_.get() != nullptr);
    FTL_DCHECK(docs.size() > 0);

    link_->SetAllDocuments(std::move(docs));
    dirty_ = false;
  }
}
}  // namespace modular
