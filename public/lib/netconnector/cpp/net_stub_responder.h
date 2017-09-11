// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_set>

#include "lib/app/cpp/application_context.h"
#include "lib/svc/cpp/service_namespace.h"
#include "lib/ftl/macros.h"
#include "lib/netconnector/fidl/netconnector.fidl.h"

namespace netconnector {

// Registers as a responding service with NetConnector and instantiates stubs
// when connection requests arrive.
template <typename TInterface, typename TStub>
class NetStubResponder {
 public:
  // Constructor. |actual| must outlive this.
  NetStubResponder(TInterface* actual,
                   const std::string& service_name,
                   app::ApplicationContext* application_context)
      : actual_(actual) {
    FTL_DCHECK(actual_);
    FTL_DCHECK(!service_name.empty());
    FTL_DCHECK(application_context);

    service_namespace_.AddServiceForName(
        [this](mx::channel channel) {
          stubs_.insert(std::shared_ptr<TStub>(
              new TStub(actual_, std::move(channel), this)));
        },
        service_name);

    netconnector::NetConnectorPtr connector =
        application_context
            ->ConnectToEnvironmentService<netconnector::NetConnector>();

    fidl::InterfaceHandle<app::ServiceProvider> handle;
    service_namespace_.AddBinding(handle.NewRequest());
    FTL_DCHECK(handle);

    connector->RegisterServiceProvider(service_name, std::move(handle));
  }

  ~NetStubResponder() { service_namespace_.Close(); }

  void ReleaseStub(std::shared_ptr<TStub> stub) { stubs_.erase(stub); }

 private:
  TInterface* actual_;
  app::ServiceNamespace service_namespace_;
  std::unordered_set<std::shared_ptr<TStub>> stubs_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetStubResponder);
};

}  // namespace netconnector
