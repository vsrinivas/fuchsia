// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <utility>

#include "garnet/bin/fonts/font_provider_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace fonts {

class App {
 public:
  App() : context_(component::StartupContext::CreateFromStartupInfo()) {
    if (!font_provider_.LoadFonts())
      exit(ZX_ERR_UNAVAILABLE);
    context_->outgoing().AddPublicService<fuchsia::fonts::FontProvider>(
        [this](fidl::InterfaceRequest<fuchsia::fonts::FontProvider> request) {
          font_provider_.AddBinding(std::move(request));
        });
  }

 private:
  std::unique_ptr<component::StartupContext> context_;
  FontProviderImpl font_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace fonts

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  fonts::App app;
  loop.Run();
  return 0;
}
