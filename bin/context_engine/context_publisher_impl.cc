// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_publisher_impl.h"

#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

ContextPublisherImpl::ContextPublisherImpl(ComponentScopePtr /* scope */,
                                           ContextRepository* repository)
    : /* source_url_(source_url), */ repository_(repository) {}
ContextPublisherImpl::~ContextPublisherImpl() = default;

void ContextPublisherImpl::Publish(const fidl::String& topic,
                                   const fidl::String& json_data) {
  if (json_data) {
    repository_->Set(topic, json_data);
  } else {
    repository_->Remove(topic);
  }
}

}  // namespace maxwell
