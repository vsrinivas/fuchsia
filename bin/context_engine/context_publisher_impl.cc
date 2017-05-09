// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_publisher_impl.h"

#include "apps/maxwell/src/context_engine/context_repository.h"
#include "apps/maxwell/src/context_engine/scope_utils.h"

namespace maxwell {

namespace {


}  // namespace

ContextPublisherImpl::ContextPublisherImpl(ComponentScopePtr scope,
                                           ContextRepository* repository)
    : scope_(std::move(scope)), repository_(repository) {}
ContextPublisherImpl::~ContextPublisherImpl() = default;

void ContextPublisherImpl::Publish(const fidl::String& topic,
                                   const fidl::String& json_data) {
  // Rewrite the topic to be within the scope specified at construction time.
  std::string local_topic = topic;
  if (scope_->is_module_scope()) {
    // If a Mod is publishing this, prefix its topic string with "explicit", to
    // indicate that the Mod is explicitly publishing this value.
    local_topic = ConcatTopic("explicit", topic);
  }
  const auto scoped_topic = ScopeAndTopicToString(scope_, local_topic);

  if (json_data) {
    repository_->Set(scoped_topic, json_data);
  } else {
    repository_->Remove(scoped_topic);
  }
}

}  // namespace maxwell
