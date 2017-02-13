// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_set>

#include "application/lib/app/application_context.h"
#include "apps/netconnector/services/netconnector_admin.fidl.h"
#include "lib/ftl/macros.h"

namespace netconnector {

// Registers as a responding service with NetConnector and instantiates stubs
// when connection requests arrive.
template <typename TInterface, typename TStub>
class NetStubResponder {
 public:
  // Constructor. |actual| must outlive this.
  NetStubResponder(TInterface* actual,
                   std::string service_name,
                   modular::ApplicationContext* application_context)
      : actual_(actual) {
    FTL_DCHECK(actual_);
    FTL_DCHECK(!service_name.empty());
    FTL_DCHECK(application_context);

    application_context->outgoing_services()->AddServiceForName(
        [this](mx::channel channel) {
          stubs_.insert(std::shared_ptr<TStub>(
              new TStub(actual_, std::move(channel), this)));
        },
        service_name);

    netconnector::NetConnectorAdminPtr admin =
        application_context
            ->ConnectToEnvironmentService<netconnector::NetConnectorAdmin>();

    fidl::InterfaceHandle<app::ServiceProvider> handle;
    application_context->outgoing_services()->AddBinding(handle.NewRequest());
    FTL_DCHECK(handle);

    admin->RegisterServiceProvider(service_name, std::move(handle));
  }

  void ReleaseStub(std::shared_ptr<TStub> stub) { stubs_.erase(stub); }

 private:
  TInterface* actual_;
  std::unordered_set<std::shared_ptr<TStub>> stubs_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetStubResponder);
};

}  // namespace netconnector
