// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_subscriber_impl.h"

#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

ContextSubscriberImpl::ContextSubscriberImpl(ContextRepository* repository)
    : repository_(repository) {}
ContextSubscriberImpl::~ContextSubscriberImpl() = default;

void ContextSubscriberImpl::Subscribe(
    const fidl::String& topic,
    fidl::InterfaceHandle<ContextSubscriberLink> link_handle) {
  ContextSubscriberLinkPtr link =
      ContextSubscriberLinkPtr::Create(std::move(link_handle));
  repository_->AddSubscription(topic, std::move(link));
}

}  // namespace maxwell
