// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/moterm/moterm_params.h"
#include "apps/moterm/moterm_view.h"
#include "apps/mozart/lib/skia/skia_font_loader.h"
#include "apps/mozart/lib/view_framework/view_provider_service.h"
#include "apps/modular/services/story/module.fidl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace moterm {

class App : public modular::Module {
 public:
  App(MotermParams params)
      : params_(std::move(params)),
        application_context_(
            modular::ApplicationContext::CreateFromStartupInfo()),
        view_provider_service_(application_context_.get(),
                               [this](mozart::ViewContext view_context) {
                                 return MakeView(std::move(view_context));
                               }),
        module_binding_(this) {
    tracing::InitializeTracer(application_context_.get(), {});

    application_context_->outgoing_services()->AddService<modular::Module>(
        [this](fidl::InterfaceRequest<modular::Module> request) {
          FTL_DCHECK(!module_binding_.is_bound());
          module_binding_.Bind(std::move(request));
        });
  }

  ~App() {}

  // modular::Module:
  void Initialize(
      fidl::InterfaceHandle<modular::Story> story,
      fidl::InterfaceHandle<modular::Link> link,
      fidl::InterfaceHandle<modular::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<modular::ServiceProvider> outgoing_services)
      override {
    story_.Bind(std::move(story));
  }

  void Stop(const StopCallback& done) override { done(); }

 private:
  std::unique_ptr<moterm::MotermView> MakeView(
      mozart::ViewContext view_context) {
    return std::make_unique<moterm::MotermView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request),
        view_context.application_context, params_);
  }

  MotermParams params_;
  std::unique_ptr<modular::ApplicationContext> application_context_;
  mozart::ViewProviderService view_provider_service_;
  fidl::Binding<modular::Module> module_binding_;
  fidl::InterfacePtr<modular::Story> story_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace moterm

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  moterm::MotermParams params;
  if (!ftl::SetLogSettingsFromCommandLine(command_line) ||
      !params.Parse(command_line)) {
    FTL_LOG(ERROR) << "Missing or invalid parameters. See README.";
    return 1;
  }

  mtl::MessageLoop loop;
  moterm::App app(std::move(params));
  loop.Run();
  return 0;
}
