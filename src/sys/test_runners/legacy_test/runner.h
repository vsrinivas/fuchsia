// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_RUNNERS_LEGACY_TEST_RUNNER_H_
#define SRC_SYS_TEST_RUNNERS_LEGACY_TEST_RUNNER_H_

#include <fuchsia/component/runner/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>

#include <map>
#include <memory>

#include "test_component.h"

using ComponentRunner = fuchsia::component::runner::ComponentRunner;

/// Implement Runner protocol and launch wrapped legacy test components.
class Runner final : public ComponentRunner {
 public:
  explicit Runner(std::shared_ptr<sys::ServiceDirectory> svc, async_dispatcher_t* dispatcher);
  ~Runner() override;

  void Start(fuchsia::component::runner::ComponentStartInfo start_info,
             fidl::InterfaceRequest<::fuchsia::component::runner::ComponentController> controller)
      override;

  fidl::InterfaceRequestHandler<ComponentRunner> GetHandler() {
    return bindings_.GetHandler(this, dispatcher_);
  }

  void AddBinding(zx::channel request) {
    bindings_.AddBinding(this, fidl::InterfaceRequest<ComponentRunner>(std::move(request)),
                         dispatcher_);
  }

 private:
  std::shared_ptr<sys::ServiceDirectory> svc_;
  std::map<TestComponent*, std::unique_ptr<TestComponent>> test_components_;
  fidl::BindingSet<ComponentRunner> bindings_;
  async_dispatcher_t* dispatcher_;
};

#endif  // SRC_SYS_TEST_RUNNERS_LEGACY_TEST_RUNNER_H_
