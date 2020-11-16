// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_BOOTSTRAP_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_BOOTSTRAP_IMPL_H_

#include <fuchsia/weave/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

namespace weavestack {

/// Handler for all fuchsia.weave/Bootstrap FIDL protocol calls. Registers as a
/// public service with the ComponentContext and handles incoming connections.
class BootstrapImpl : public fuchsia::weave::Bootstrap {
 public:
  // Construct a new instance of |BootstrapImpl|.
  //
  // This method does not take ownership of the |context|.
  explicit BootstrapImpl(sys::ComponentContext* context);
  ~BootstrapImpl();

  // Initialize and register this instance as FIDL handler.
  zx_status_t Init();

  // Implementation of the fuchsia.weave.Bootstrap interface.
  void ImportWeaveConfig(fuchsia::mem::Buffer config_json,
                         ImportWeaveConfigCallback callback) override;

 private:
  // Prevent copy/move construction
  BootstrapImpl(const BootstrapImpl&) = delete;
  BootstrapImpl(BootstrapImpl&&) = delete;
  // Prevent copy/move assignment
  BootstrapImpl& operator=(const BootstrapImpl&) = delete;
  BootstrapImpl& operator=(BootstrapImpl&&) = delete;

  // Get the path to the config file to be written by this implementation.
  virtual std::string GetConfigPath();
  // Determine if this FIDL should be served.
  virtual bool ShouldServe();

  // FIDL servicing related state
  fidl::BindingSet<fuchsia::weave::Bootstrap> bindings_;
  sys::ComponentContext* context_;
  bool serving_ = false;
};

}  // namespace weavestack

#endif  // SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_BOOTSTRAP_IMPL_H_
