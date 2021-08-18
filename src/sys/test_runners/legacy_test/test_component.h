// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_RUNNERS_LEGACY_TEST_TEST_COMPONENT_H_
#define SRC_SYS_TEST_RUNNERS_LEGACY_TEST_TEST_COMPONENT_H_

#include <fuchsia/component/runner/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include <garnet/bin/run_test_component/test_metadata.h>

#include "suite.h"

using ComponentController = fuchsia::component::runner::ComponentController;

struct TestComponentArgs {
  std::string legacy_url;
  zx::channel outgoing_dir;
  fuchsia::sys::EnvironmentPtr parent_env;
  std::shared_ptr<sys::ServiceDirectory> parent_env_svc;
  std::shared_ptr<sys::ServiceDirectory> test_component_svc;
  std::vector<fuchsia::component::runner::ComponentNamespaceEntry> ns;
  std::shared_ptr<run::TestMetadata> test_metadata;

  fidl::InterfaceRequest<ComponentController> request;
  async_dispatcher_t* dispatcher;
};

/// Implements component controller on behalf of the runner and also
/// stores/controls running test component.
class TestComponent final : public ComponentController {
  using DoneCallback = fit::function<void(TestComponent*)>;

 public:
  explicit TestComponent(TestComponentArgs args, DoneCallback done_callback);
  ~TestComponent() override;

  void Stop() override;

  void Kill() override;

 private:
  async_dispatcher_t* dispatcher_;
  fidl::Binding<ComponentController> binding_;

  /// For safe keeping while the component is running.
  std::vector<fuchsia::component::runner::ComponentNamespaceEntry> ns_;
  std::unique_ptr<Suite> suite_;

  /// Exposes suite protocol on behalf of test component.
  std::unique_ptr<sys::ComponentContext> suite_context_;

  DoneCallback done_callback_;
};

#endif  // SRC_SYS_TEST_RUNNERS_LEGACY_TEST_TEST_COMPONENT_H_
