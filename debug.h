// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/interfaces/debug.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/utility/run_loop.h"

namespace maxwell {

// Debug service implementation that grants the maxwell_test harness a license
// to kill the instrumented Mojo app. To support this, the Mojo app should
// include a DebugSupport member and call AddService in its OnAcceptConnection
// implementation. For convenience, apps that do not themselves provide any
// other services may instead inherit from DebuggableApp rather than
// ApplicationImplBase.
class DebugSupport : public Debug, public TestParent {
 public:
  virtual ~DebugSupport() {}

  void Kill() override { mojo::RunLoop::current()->Quit(); }

  // This implementation forwards child dependency registration to the parent
  // that spawned this dependency.
  void RegisterChildDependency(const mojo::String& url) override {
    parent_->RegisterChildDependency(url);
  }

  // This method is intended to be called from a Mojo OnAcceptConnection
  // implementation to add the Debug and TestParent instrumentation services to
  // the Mojo app.
  void AddService(mojo::Shell* shell,
                  mojo::ServiceProviderImpl* service_provider_impl) {
    service_provider_impl->AddService<Debug>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<Debug> request) {
          // For now, whitelist the test harness.
          if (connection_context.remote_url == kTestApp) {
            debug_bindings_.AddBinding(this, std::move(request));
          }
        });
    service_provider_impl->AddService<TestParent>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<TestParent> request) {
          test_parent_bindings_.AddBinding(this, std::move(request));

          // Also register the requester as a new transitive dependency.
          parent_->RegisterChildDependency(connection_context.remote_url);
        });

    // If the remote Mojo app calling ConnectToService on this Mojo app exposes
    // a TestParent interface, then this Mojo app should register itself with
    // the remote TestParent.
    if (!registered_) {
      ConnectToService(shell,
                       service_provider_impl->connection_context().remote_url,
                       GetProxy(&parent_));
      registered_ = true;
    }
  }

 private:
  static const char kTestApp[];

  mojo::BindingSet<Debug> debug_bindings_;
  mojo::BindingSet<TestParent> test_parent_bindings_;
  TestParentPtr parent_;

  // TODO(rosswang): remove this if InterfacePtrSet becomes a real set
  bool registered_;
};

const char DebugSupport::kTestApp[] = "mojo:maxwell_test";

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

}  // namespace maxwell
