// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_SERVICE_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_SERVICE_H_

#include <fuchsia/netemul/sandbox/cpp/fidl.h>

#include "sandbox.h"

namespace netemul {

class SandboxBinding;
class SandboxService {
 public:
  explicit SandboxService(async_dispatcher_t* dispatcher);
  ~SandboxService();

  fidl::InterfaceRequestHandler<fuchsia::netemul::sandbox::Sandbox>
  GetHandler();

 protected:
  friend SandboxBinding;
  void BindingClosed(SandboxBinding* binding);

 private:
  async_dispatcher_t* dispatcher_;
  std::vector<std::unique_ptr<SandboxBinding>> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SandboxService);
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_SERVICE_H_
