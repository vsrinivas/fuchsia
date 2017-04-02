// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/graph.h"

namespace maxwell {

void DataNode::Update(const fidl::String& json_value) {
  json_value_ = json_value;

  class ContextUpdate update;
  update.source = component_->url;
  update.json_value = json_value;

  for (const ContextSubscriberLinkPtr& subscriber : subscribers_) {
    subscriber->OnUpdate(update.Clone());
  }
}

void DataNode::Subscribe(ContextSubscriberLinkPtr link) {
  // If there is already context, send it as an initial update. If it could
  // be stale, it is up to the publisher to have removed it.
  if (!json_value_.empty()) {
    auto update = ContextUpdate::New();
    update->source = component_->url;
    update->json_value = json_value_;
    link->OnUpdate(std::move(update));
  }

  subscribers_.emplace(std::move(link));
}

void DataNode::SetPublisher(
    fidl::InterfaceRequest<ContextPublisherLink> link_request) {
  publisher_.Bind(std::move(link_request));
}

}  // namespace maxwell
