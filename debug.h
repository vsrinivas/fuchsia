// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/interfaces/debug.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace maxwell {

// Debug service implementation that grants the maxwell_test harness a license
// to kill the instrumented Mojo app. To support this, the Mojo app should
// include a DebugSupport member and call AddService in its OnAcceptConnection
// implementation. For convenience, apps that do not themselves provide any
// other services may instead inherit from DebuggableApp rather than
// ApplicationImplBase.
class DebugSupport : public Debug {
 public:
  virtual ~DebugSupport() {}

  void Kill() override;

  // This method is intended to be called from a Mojo OnAcceptConnection
  // implementation to add the Debug and TestParent instrumentation services to
  // the Mojo app.
  void AddService(mojo::Shell* shell,
                  mojo::ServiceProviderImpl* service_provider_impl);

 private:
  static const char kTestApp[];
  mojo::BindingSet<Debug> debug_bindings_;
};

// Convenience base class for debuggable Mojo apps that do not need to expose
// other services. Apps inheriting from this should not themselves inherit
// directly from ApplicationImplBase.
class DebuggableApp : public mojo::ApplicationImplBase {
 public:
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    debug_.AddService(shell(), service_provider_impl);
    return true;
  }

 private:
  DebugSupport debug_;
};

template <class Interface>
DebugPtr ConnectToDebuggableService(mojo::Shell* shell,
                                    const std::string& url,
                                    mojo::InterfaceRequest<Interface> request) {
  mojo::ServiceProviderPtr service_provider;
  shell->ConnectToApplication(url, GetProxy(&service_provider));
  ConnectToService(service_provider.get(), std::move(request));
  DebugPtr debug;
  ConnectToService(service_provider.get(), GetProxy(&debug));
  return debug;
}

}  // namespace maxwell
