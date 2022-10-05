// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_COMPONENT_EVENT_PROVIDER_IMPL_H_

#define SRC_SYS_APPMGR_COMPONENT_EVENT_PROVIDER_IMPL_H_

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/async/cpp/executor.h>

#include <queue>

#include "lib/fidl/cpp/binding.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/sys/appmgr/component_controller_impl.h"

namespace component {

class ComponentEventProviderImpl : public fuchsia::sys::internal::ComponentEventProvider {
 public:
  // Does not take ownership of realm or dispatcher.
  ComponentEventProviderImpl(fxl::WeakPtr<Realm> realm, async_dispatcher_t* dispatcher);
  ~ComponentEventProviderImpl() override;

  bool listener_bound() { return listener_.is_bound(); }

  // |fuchsia::sys::internal::ComponentEventProvider|
  void SetListener(
      fidl::InterfaceHandle<fuchsia::sys::internal::ComponentEventListener> listener) override;

  // Requests to bind the incoming |ComponentEventProvider| connection.
  zx_status_t Connect(
      fidl::InterfaceRequest<fuchsia::sys::internal::ComponentEventProvider> request);

  // Requests to notify the listener that a component stopped.
  void NotifyComponentStopped(fuchsia::sys::internal::SourceIdentity component);

  // Requests to notify the listener that a component out/diagnostics directory is ready.
  void NotifyComponentDirReady(fuchsia::sys::internal::SourceIdentity component,
                               fidl::InterfaceHandle<fuchsia::io::Directory> directory);

 private:
  // Send Start and Diagnostics directory ready events for all components in this realm and children
  // realms.
  void NotifyOfExistingComponents();

  // Send Start and Diagnostics directory for the given component.
  void NotifyAboutExistingComponent(std::vector<std::string> relative_realm_path,
                                    std::shared_ptr<ComponentControllerBase> application);

  // Not owned.
  async::Executor executor_;
  fidl::Binding<fuchsia::sys::internal::ComponentEventProvider> binding_;
  fuchsia::sys::internal::ComponentEventListenerPtr listener_;

  // The realm to which this ComponentEventProvider belongs. The provider will only notify about
  // events of components in this realm and sub-realms, except for realms that have a provider.
  // Not owned.
  fxl::WeakPtr<Realm> const realm_;

  // Returns the relative realm path from the queries |leaf_realm| up to this provider |realm_|.
  std::vector<std::string> RelativeRealmPath(const fxl::WeakPtr<Realm>& leaf_realm);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_COMPONENT_EVENT_PROVIDER_IMPL_H_
