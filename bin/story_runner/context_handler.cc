// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/context_handler.h"

#include "apps/maxwell/services/context/context_reader.fidl.h"

namespace modular {

ContextHandler::ContextHandler(
    maxwell::IntelligenceServices* const intelligence_services)
    : value_(maxwell::ContextUpdate::New()), binding_(this) {
  intelligence_services->GetContextReader(context_reader_.NewRequest());
  query_.topics.resize(0);
}

ContextHandler::~ContextHandler() = default;

void ContextHandler::Select(const fidl::String& topic) {
  if (binding_.is_bound()) {
    binding_.Close();
  }

  query_.topics.push_back(topic);
  context_reader_->SubscribeToTopics(query_.Clone(), binding_.NewBinding());
}

void ContextHandler::Watch(const std::function<void()>& watcher) {
  watchers_.push_back(watcher);
}

void ContextHandler::OnUpdate(maxwell::ContextUpdatePtr value) {
  value_ = std::move(value);

  for (auto& watcher : watchers_) {
    watcher();
  }
}

}  // namespace modular
