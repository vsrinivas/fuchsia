// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/context_handler.h"

#include "lib/context/fidl/context_reader.fidl.h"

namespace modular {

ContextHandler::ContextHandler(
    maxwell::IntelligenceServices* const intelligence_services)
    : binding_(this) {
  intelligence_services->GetContextReader(context_reader_.NewRequest());
}

ContextHandler::~ContextHandler() = default;

void ContextHandler::SelectTopics(const std::vector<fidl::String>& topics) {
  if (binding_.is_bound()) {
    binding_.Close();
  }

  auto query = maxwell::ContextQuery::New();
  for (const auto& topic : topics) {
    auto selector = maxwell::ContextSelector::New();
    selector->type = maxwell::ContextValueType::ENTITY;
    selector->meta = maxwell::ContextMetadata::New();
    ;
    selector->meta->entity = maxwell::EntityMetadata::New();
    selector->meta->entity->topic = topic;
    query->selector[topic] = std::move(selector);
  }

  context_reader_->Subscribe(std::move(query), binding_.NewBinding());
}

void ContextHandler::Watch(const std::function<void()>& watcher) {
  watchers_.push_back(watcher);
}

void ContextHandler::OnContextUpdate(maxwell::ContextUpdatePtr update) {
  state_ = fidl::Map<fidl::String, fidl::String>();
  for (const auto& it : update->values) {
    if (it.GetValue().empty())
      continue;
    // HACK(thatguy): It is possible to have more than one value come back per
    // ContextSelector. Use just the first value, as that will at least be
    // deterministically the first until the value is deleted.
    state_[it.GetKey()] = it.GetValue()[0]->content;
  }

  for (auto& watcher : watchers_) {
    watcher();
  }
}

}  // namespace modular
