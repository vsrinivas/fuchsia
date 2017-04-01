// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/context/context_subscriber.fidl.h"
#include "apps/maxwell/src/context_engine/repo.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace maxwell {

class ContextEngineImpl : public ContextEngine {
 public:
  ContextEngineImpl();
  ~ContextEngineImpl() override;

 private:
  // |ContextEngine|
  void RegisterPublisher(
      const fidl::String& url,
      fidl::InterfaceRequest<ContextPublisher> request) override;

  // |ContextEngine|
  void RegisterSubscriber(
      const fidl::String& url,
      fidl::InterfaceRequest<ContextSubscriber> request) override;

  Repo repo_;

  fidl::BindingSet<ContextPublisher, std::unique_ptr<ContextPublisher>>
      publisher_bindings_;
  fidl::BindingSet<ContextSubscriber, std::unique_ptr<ContextSubscriber>>
      subscriber_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextEngineImpl);
};

}  // namespace maxwell
