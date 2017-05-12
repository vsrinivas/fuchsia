// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/composition2/composer.fidl.h"
#include "apps/mozart/src/composer/composer_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mozart {
namespace composer {

class ComposerImpl;

class HelloComposerService {
 public:
  explicit HelloComposerService()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
    FTL_DCHECK(application_context_);

    application_context_->outgoing_services()->AddService<mozart2::Composer>(
        [this](fidl::InterfaceRequest<mozart2::Composer> request) {
          composer_bindings_.AddBinding(std::make_unique<ComposerImpl>(),
                                        std::move(request));
        });
  }
  ~HelloComposerService(){};

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;

  fidl::BindingSet<mozart2::Composer, std::unique_ptr<ComposerImpl>>
      composer_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HelloComposerService);
};

}  // namespace composer
}  // namespace mozart

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  mozart::composer::HelloComposerService composer_app;

  loop.Run();
  return 0;
}
