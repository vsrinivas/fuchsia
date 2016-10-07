// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/context_engine/graph.h"

namespace maxwell {
namespace context_engine {

void DataNode::SubscriberSet::OnConnectionError(
    ContextSubscriberLink* interface_ptr) {
  MOJO_LOG(VERBOSE) << "Subscription to " << node_->label << " lost";

  ExtensibleInterfacePtrSet<ContextSubscriberLink>::OnConnectionError(
      interface_ptr);

  // Notify if this was the last subscriber.
  if (node_->subscribers_.empty() && node_->publisher_controller_) {
    MOJO_LOG(VERBOSE) << "No more subscribers to " << node_->label;
    node_->publisher_controller_->OnNoSubscribers();
  }
}

void DataNode::Update(const mojo::String& json_value) {
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
    link->OnUpdate(update.Pass());
  }

  // Notify if this is the first subscriber.
  if (subscribers_.empty() && publisher_controller_) {
    publisher_controller_->OnHasSubscribers();
  }

  subscribers_.emplace(link.Pass());
}

void DataNode::SetPublisher(
    mojo::InterfaceHandle<ContextPublisherController> controller_handle,
    mojo::InterfaceRequest<ContextPublisherLink> link_request) {
  if (controller_handle) {
    ContextPublisherControllerPtr controller =
        ContextPublisherControllerPtr::Create(controller_handle.Pass());

    // Immediately notify if there are already subscribers.
    if (!subscribers_.empty())
      controller->OnHasSubscribers();

    publisher_controller_ = controller.Pass();
  } else {
    publisher_controller_.reset();
  }

  publisher_.Bind(link_request.Pass());
}

}  // namespace context_engine
}  // namespace maxwell
