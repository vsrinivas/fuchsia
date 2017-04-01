// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_publisher_impl.h"

#include "apps/maxwell/src/context_engine/graph.h"
#include "apps/maxwell/src/context_engine/repo.h"

namespace maxwell {

ContextPublisherImpl::ContextPublisherImpl(ComponentNode* component, Repo* repo)
    : component_(component), repo_(repo) {}
ContextPublisherImpl::~ContextPublisherImpl() = default;

void ContextPublisherImpl::Publish(
    const fidl::String& label,
    fidl::InterfaceHandle<ContextPublisherController> controller,
    fidl::InterfaceRequest<ContextPublisherLink> link) {
  DataNode* output = component_->EmplaceDataNode(label);
  repo_->Index(output);
  output->SetPublisher(std::move(controller), std::move(link));
}

}  // namespace maxwell
