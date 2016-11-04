// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "apps/fonts/font_provider_impl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace fonts {

class App {
 public:
  App() : context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    if (!font_provider_.LoadFonts())
      exit(ERR_UNAVAILABLE);
    context_->outgoing_services()->AddService<FontProvider>(
        [this](fidl::InterfaceRequest<FontProvider> request) {
          font_provider_.AddBinding(std::move(request));
        });
  }

 private:
  std::unique_ptr<modular::ApplicationContext> context_;
  FontProviderImpl font_provider_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace fonts

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  fonts::App app;
  loop.Run();
  return 0;
}
