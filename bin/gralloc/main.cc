// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <memory>

#include <fuchsia/gralloc/cpp/fidl.h>
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/logging.h"

class GrallocImpl : public fuchsia::gralloc::Gralloc {
 public:
  // |fuchsia::gralloc::Gralloc|
  void Allocate(uint64_t size, AllocateCallback callback) override {
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);

    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Gralloc failed to allocate VMO of size: " << size
                       << " status: " << status;
    }

    callback(std::move(vmo));
  }
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> app_context(
      component::StartupContext::CreateFromStartupInfo());

  GrallocImpl grallocator;

  fidl::BindingSet<fuchsia::gralloc::Gralloc> bindings;

  app_context->outgoing().AddPublicService(bindings.GetHandler(&grallocator));

  loop.Run();

  return 0;
}
