// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_publisher_impl.h"

#include "apps/maxwell/src/context_engine/repo.h"

namespace maxwell {

ContextPublisherImpl::ContextPublisherImpl(const std::string& /* source_url */,
                                           Repo* repo)
    : /* source_url_(source_url), */ repo_(repo) {}
ContextPublisherImpl::~ContextPublisherImpl() = default;

void ContextPublisherImpl::Publish(const fidl::String& topic,
                                   const fidl::String& json_data) {
  if (json_data) {
    repo_->Set(topic, json_data);
  } else {
    repo_->Remove(topic);
  }
}

}  // namespace maxwell
