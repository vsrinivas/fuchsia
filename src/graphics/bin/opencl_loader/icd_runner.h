// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_BIN_OPENCL_LOADER_ICD_RUNNER_H_
#define SRC_GRAPHICS_BIN_OPENCL_LOADER_ICD_RUNNER_H_

#include <fuchsia/component/runner/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

// This implements the icd_runner interface.
class IcdRunnerImpl : public fuchsia::component::runner::ComponentRunner {
 public:
  void Add(const std::shared_ptr<sys::OutgoingDirectory>& outgoing, async_dispatcher_t* dispatcher);

 private:
  void Start(
      fuchsia::component::runner::ComponentStartInfo start_info,
      fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller) override;

  fidl::BindingSet<fuchsia::component::runner::ComponentRunner> bindings_;
};

#endif  // SRC_GRAPHICS_BIN_OPENCL_LOADER_ICD_RUNNER_H_
