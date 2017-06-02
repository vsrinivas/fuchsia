// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/context_handler.h"

#include "apps/maxwell/services/context/context_provider.fidl.h"

namespace modular {

ContextHandler::ContextHandler(
    maxwell::IntelligenceServices* const intelligence_services)
    : binding_(this) {
  intelligence_services->GetContextProvider(context_provider_.NewRequest());

  auto context_query = maxwell::ContextQuery::New();
  context_query->topics.resize(0);

  context_provider_->Subscribe(std::move(context_query), binding_.NewBinding());
}

ContextHandler::~ContextHandler() = default;

void ContextHandler::OnUpdate(maxwell::ContextUpdatePtr value) {
  value_ = std::move(value);
}

}  // namespace modular
