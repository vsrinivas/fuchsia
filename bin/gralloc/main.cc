// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <lib/async-loop/cpp/loop.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/logging.h"
#include <gralloc/cpp/fidl.h>

class GrallocImpl : public gralloc::Gralloc {
 public:
  // |gralloc::Gralloc|
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
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  std::unique_ptr<component::ApplicationContext> app_context(
      component::ApplicationContext::CreateFromStartupInfo());

  GrallocImpl grallocator;

  fidl::BindingSet<gralloc::Gralloc> bindings;

  app_context->outgoing().AddPublicService<gralloc::Gralloc>(
      [&grallocator,
       &bindings](fidl::InterfaceRequest<gralloc::Gralloc> request) {
        bindings.AddBinding(&grallocator, std::move(request));
      });

  loop.Run();

  return 0;
}
