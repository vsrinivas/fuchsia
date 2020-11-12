// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_STACK_PROVIDER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_STACK_PROVIDER_IMPL_H_

#include <fuchsia/weave/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

namespace weavestack {

// Handler for all fuchsia.weave/StackProvider FIDL protocol calls. Registers as a
// public service with the ComponentContext and handles incoming connections.
class StackProviderImpl : public fuchsia::weave::StackProvider {
 public:
  // Construct a new instance of |StackProviderImpl|.
  //
  // This method does not take ownership of the |context|.
  explicit StackProviderImpl(sys::ComponentContext* context);
  virtual ~StackProviderImpl();

  // Initialize and register this instance as FIDL handler.
  zx_status_t Init();

  // Set a |WlanNetworkConfigProvider| to get WLAN network config information.
  void SetWlanNetworkConfigProvider(
      ::fidl::InterfaceHandle<class ::fuchsia::weave::WlanNetworkConfigProvider> provider) override;

 private:
  // Prevent copy/move construction
  StackProviderImpl(const StackProviderImpl&) = delete;
  StackProviderImpl(StackProviderImpl&&) = delete;
  // Prevent copy/move assignment
  StackProviderImpl& operator=(const StackProviderImpl&) = delete;
  StackProviderImpl& operator=(StackProviderImpl&&) = delete;

  // FIDL servicing related state
  fidl::BindingSet<fuchsia::weave::StackProvider> bindings_;
  fit::function<void(::fuchsia::wlan::policy::NetworkConfig)> wlan_network_update_callback_;
  fuchsia::weave::WlanNetworkConfigProviderPtr wlan_network_config_provider_;
  sys::ComponentContext* context_;
};

}  // namespace weavestack

#endif  // SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_STACK_PROVIDER_IMPL_H_
