// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_publisher.fidl.h"

namespace maxwell {

class ComponentNode;
class Repo;

class ContextPublisherImpl : public ContextPublisher {
 public:
  ContextPublisherImpl(ComponentNode* component, Repo* repo);
  ~ContextPublisherImpl() override;

 private:
  // |ContextPublisher|
  void Publish(const fidl::String& label,
               fidl::InterfaceHandle<ContextPublisherController> controller,
               fidl::InterfaceRequest<ContextPublisherLink> link) override;

  ComponentNode* component_;
  Repo* repo_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextPublisherImpl);
};

}  // namespace maxwell
