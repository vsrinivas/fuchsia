// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETCONNECTOR_CPP_NET_STUB_RESPONDER_H_
#define LIB_NETCONNECTOR_CPP_NET_STUB_RESPONDER_H_

#include <fuchsia/netconnector/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <unordered_set>

#include "lib/svc/cpp/service_namespace.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace netconnector {

// Registers as a responding service with NetConnector and instantiates stubs
// when connection requests arrive.
template <typename TInterface, typename TStub>
class NetStubResponder {
 public:
  // Constructor. |actual| must outlive this.
  NetStubResponder(const fidl::InterfacePtr<TInterface>& actual,
                   const std::string& service_name,
                   sys::ComponentContext* component_context)
      : actual_(actual) {
    FXL_DCHECK(actual_);
    FXL_DCHECK(!service_name.empty());
    FXL_DCHECK(component_context);

    service_namespace_.AddServiceForName(
        [this](zx::channel channel) {
          stubs_.insert(std::shared_ptr<TStub>(
              new TStub(actual_, std::move(channel), this)));
        },
        service_name);

    fuchsia::netconnector::NetConnectorPtr connector =
        component_context->svc()
            ->Connect<fuchsia::netconnector::NetConnector>();

    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> handle;
    service_namespace_.AddBinding(handle.NewRequest());
    FXL_DCHECK(handle);

    connector->RegisterServiceProvider(service_name, std::move(handle));
  }

  ~NetStubResponder() { service_namespace_.Close(); }

  void ReleaseStub(std::shared_ptr<TStub> stub) { stubs_.erase(stub); }

 private:
  const fidl::InterfacePtr<TInterface>& actual_;
  component::ServiceNamespace service_namespace_;
  std::unordered_set<std::shared_ptr<TStub>> stubs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetStubResponder);
};

}  // namespace netconnector

#endif  // LIB_NETCONNECTOR_CPP_NET_STUB_RESPONDER_H_
