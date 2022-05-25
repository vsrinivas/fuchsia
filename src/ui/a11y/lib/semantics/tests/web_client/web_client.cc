// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>

#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/ui/a11y/lib/semantics/tests/web_client/web_client_config_lib.h"

namespace {

fuchsia::mem::Buffer BufferFromString(const std::string& script) {
  fuchsia::mem::Buffer buffer;
  uint64_t num_bytes = script.size();

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(num_bytes, 0u, &vmo);
  FX_CHECK(status >= 0);

  status = vmo.write(script.data(), 0, num_bytes);
  FX_CHECK(status >= 0);
  buffer.vmo = std::move(vmo);
  buffer.size = num_bytes;

  return buffer;
}

class NavListener : public fuchsia::web::NavigationEventListener {
 public:
  // |fuchsia::web::NavigationEventListener|
  void OnNavigationStateChanged(fuchsia::web::NavigationState nav_state,
                                OnNavigationStateChangedCallback send_ack) override {
    if (nav_state.has_url()) {
      FX_VLOGS(1) << "nav_state.url = " << nav_state.url();
    }
    if (nav_state.has_page_type()) {
      FX_VLOGS(1) << "nav_state.page_type = " << static_cast<size_t>(nav_state.page_type());
    }
    if (nav_state.has_is_main_document_loaded()) {
      FX_LOGS(INFO) << "nav_state.is_main_document_loaded = "
                    << nav_state.is_main_document_loaded();
    }
    send_ack();
  }
};

// Implements a simple web app, which responds to touch events.
class WebApp : public fuchsia::ui::app::ViewProvider {
 public:
  WebApp()
      : loop_(&kAsyncLoopConfigAttachToCurrentThread),
        context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        view_provider_binding_(this) {
    FX_LOGS(INFO) << "Starting web client";
    SetupWebEngine();
    SetupViewProvider();
  }

  void Run() {
    // Set up navigation affordances.
    FX_LOGS(INFO) << "Loading web app";
    fuchsia::web::NavigationControllerPtr navigation_controller;
    NavListener navigation_event_listener;
    fidl::Binding<fuchsia::web::NavigationEventListener> navigation_event_listener_binding(
        &navigation_event_listener);
    web_frame_->SetNavigationEventListener(navigation_event_listener_binding.NewBinding());
    web_frame_->GetNavigationController(navigation_controller.NewRequest());

    // Read HTML from structured config file.
    auto web_client_config = web_client_config_lib::Config::TakeFromStartupHandle();
    FX_CHECK(!web_client_config.html().empty());

    // Load the web page.
    FX_LOGS(INFO) << "Loading web page";
    navigation_controller->LoadUrl("about:blank", fuchsia::web::LoadUrlParams(), [](auto result) {
      if (result.is_err()) {
        FX_LOGS(FATAL) << "Error while loading URL: " << static_cast<uint32_t>(result.err());
      } else {
        FX_LOGS(INFO) << "Loaded about:blank";
      }
    });

    FX_LOGS(INFO) << "Running javascript to inject html: " << web_client_config.html();
    web_frame_->ExecuteJavaScript({"*"},
                                  BufferFromString(fxl::StringPrintf(
                                      "document.write(`%s`);", web_client_config.html().c_str())),
                                  [](auto result) {
                                    if (result.is_err()) {
                                      FX_LOGS(FATAL) << "Error while executing JavaScript: "
                                                     << static_cast<uint32_t>(result.err());
                                    } else {
                                      FX_LOGS(INFO) << "Injected html";
                                    }
                                  });

    loop_.Run();
  }

 private:
  void SetupWebEngine() {
    auto web_context_provider = context_->svc()->Connect<fuchsia::web::ContextProvider>();
    auto incoming_service_clone = context_->svc()->CloneChannel();
    web_context_provider.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "web_context_provider: " << zx_status_get_string(status);
    });
    FX_CHECK(incoming_service_clone.is_valid());

    fuchsia::web::CreateContextParams params;
    params.set_service_directory(std::move(incoming_service_clone));
    web_context_provider->Create(std::move(params), web_context_.NewRequest());
    web_context_.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "web_context_: " << zx_status_get_string(status);
    });
    web_context_->CreateFrame(web_frame_.NewRequest());
    web_frame_.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "web_frame_: " << zx_status_get_string(status);
    });
    web_frame_->SetJavaScriptLogLevel(fuchsia::web::ConsoleLogLevel::ERROR);
  }

  void SetupViewProvider() {
    fidl::InterfaceRequestHandler<fuchsia::ui::app::ViewProvider> handler =
        [&](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          if (view_provider_binding_.is_bound()) {
            request.Close(ZX_ERR_ALREADY_BOUND);
            return;
          }
          view_provider_binding_.Bind(std::move(request));
        };
    context_->outgoing()->AddPublicService(std::move(handler));
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override {
    web_frame_->CreateView(scenic::ToViewToken(std::move(token)));
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override {
    web_frame_->CreateViewWithViewRef(scenic::ToViewToken(std::move(token)),
                                      std::move(view_ref_control), std::move(view_ref));
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override {
    fuchsia::web::CreateView2Args args2;
    fuchsia::ui::views::ViewCreationToken token;
    args2.set_view_creation_token(std::move(*args.mutable_view_creation_token()));
    web_frame_->CreateView2(std::move(args2));
  }

  template <typename PredicateT>
  void RunLoopUntil(PredicateT predicate) {
    while (!predicate()) {
      loop_.Run(zx::time::infinite(), true);
    }
  }

  async::Loop loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;
  fuchsia::web::ContextPtr web_context_;
  fuchsia::web::FramePtr web_frame_;
};
}  // namespace

int main(int argc, const char** argv) { WebApp().Run(); }
