// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/services/story/module.fidl.h"
#include "apps/moterm/history.h"
#include "apps/moterm/ledger_helpers.h"
#include "apps/moterm/moterm_params.h"
#include "apps/moterm/moterm_view.h"
#include "apps/mozart/lib/skia/skia_font_loader.h"
#include "apps/mozart/lib/view_framework/view_provider_service.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace moterm {

namespace {
ledger::PagePtr GetPageProxyForRequest(
    fidl::InterfaceRequest<ledger::Page>* request) {
  ledger::PagePtr page;
  *request = page.NewRequest();
  return page;
}

}  // namespace

class App : public modular::Module {
 public:
  App(MotermParams params)
      : params_(std::move(params)),
        application_context_(
            app::ApplicationContext::CreateFromStartupInfo()),
        view_provider_service_(application_context_.get(),
                               [this](mozart::ViewContext view_context) {
                                 return MakeView(std::move(view_context));
                               }),
        module_binding_(this),
        history_(GetPageProxyForRequest(&history_page_request_)) {
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
      fidl::InterfaceHandle<modular::Story> story_handle,
      fidl::InterfaceHandle<modular::Link> link_handle,
      fidl::InterfaceHandle<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services)
      override {
    fidl::InterfacePtr<modular::Story> story;
    story.Bind(std::move(story_handle));

    ledger::LedgerPtr ledger;
    modular::Story* story_ptr = story.get();
    story_ptr->GetLedger(
        ledger.NewRequest(),
        ftl::MakeCopyable([story = std::move(story)](ledger::Status status) {
          LogLedgerError(status, "GetLedger");
        }));

    ledger::Ledger* ledger_ptr = ledger.get();
    ledger_ptr->GetRootPage(
        std::move(history_page_request_),
        ftl::MakeCopyable([ledger = std::move(ledger)](ledger::Status status) {
          LogLedgerError(status, "GetRootPage");
        }));
  }

  void Stop(const StopCallback& done) override { done(); }

 private:
  std::unique_ptr<moterm::MotermView> MakeView(
      mozart::ViewContext view_context) {
    return std::make_unique<moterm::MotermView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request),
        view_context.application_context, &history_, params_);
  }

  MotermParams params_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  mozart::ViewProviderService view_provider_service_;
  fidl::Binding<modular::Module> module_binding_;
  fidl::InterfaceRequest<ledger::Page> history_page_request_;
  // Ledger-backed store for terminal history.
  History history_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace moterm

int main(int argc, const char** argv) {
  srand(mx_time_get(MX_CLOCK_UTC));

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
