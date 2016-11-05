// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/context_engine/graph.h"

namespace maxwell {
namespace context_engine {

void DataNode::SubscriberSet::OnConnectionError(
    ContextSubscriberLink* interface_ptr) {
  BoundPtrSet<ContextSubscriberLink>::OnConnectionError(interface_ptr);

  // Notify if this was the last subscriber.
  if (node_->subscribers_.empty() && node_->publisher_controller_) {
    node_->publisher_controller_->OnNoSubscribers();
  }
}

void DataNode::Update(const fidl::String& json_value) {
  json_value_ = json_value;

  ContextUpdate update;
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
    ContextUpdatePtr update = ContextUpdate::New();
    update->source = component_->url;
    update->json_value = json_value_;
    link->OnUpdate(std::move(update));
  }

  // Notify if this is the first subscriber.
  if (subscribers_.empty() && publisher_controller_) {
    publisher_controller_->OnHasSubscribers();
  }

  subscribers_.emplace(std::move(link));
}

void DataNode::SetPublisher(
    fidl::InterfaceHandle<ContextPublisherController> controller_handle,
    fidl::InterfaceRequest<ContextPublisherLink> link_request) {
  if (controller_handle) {
    ContextPublisherControllerPtr controller =
        ContextPublisherControllerPtr::Create(std::move(controller_handle));

    // Immediately notify if there are already subscribers.
    if (!subscribers_.empty())
      controller->OnHasSubscribers();

    publisher_controller_ = std::move(controller);
  } else {
    publisher_controller_.reset();
  }

  publisher_.Bind(std::move(link_request));
}

}  // namespace context_engine
}  // namespace maxwell
