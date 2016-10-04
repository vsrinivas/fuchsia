// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "graph.h"

namespace intelligence {
namespace context_service {

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
  // Taken from mojo::InterfacePtrSet; remove link on error.
  ContextSubscriberLink* ifc = link.get();
  link.set_connection_error_handler([this, ifc] {
    // General note: since Mojo message processing, including error handling,
    // is single-threaded, this is guaranteed not to happen at least until the
    // next processing loop.

    MOJO_LOG(VERBOSE) << "Subscription to " << label << " lost";

    auto it = std::find_if(subscribers_.begin(), subscribers_.end(),
                           [ifc](const ContextSubscriberLinkPtr& sub) {
                             return sub.get() == ifc;
                           });

    assert(it != subscribers_.end());

    // Notify if this was the last subscriber.
    if (subscribers_.size() == 1 && publisher_controller_) {
      MOJO_LOG(VERBOSE) << "No more subscribers to " << label;
      publisher_controller_->OnNoSubscribers();
    }

    // This must be the last line in the error handler, because once we do
    // this the lambda is destroyed and subsequent capture accesses seg-fault.
    subscribers_.erase(it);
  });

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

  subscribers_.emplace_back(link.Pass());
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

}  // namespace context_service
}  // namespace intelligence
