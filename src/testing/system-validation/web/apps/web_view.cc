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
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>

#include "src/testing/system-validation/web/apps/web_view_config_lib.h"

namespace {

fuchsia::mem::Buffer LoadFileToBuffer(const std::string& filePath) {
  fuchsia::mem::Buffer buffer;
  zx::vmo vmo;

  FILE* fp = fopen(filePath.c_str(), "rb");
  FX_CHECK(fp) << "failed to opoen file " << filePath;

  fseek(fp, 0, SEEK_END);
  uint64_t num_bytes = ftell(fp);
  rewind(fp);

  zx_status_t status = zx::vmo::create(num_bytes, 0u, &vmo);
  FX_CHECK(status >= 0);

  std::string contents;
  contents.resize(num_bytes);
  fread(contents.data(), 1, contents.size(), fp);

  status = vmo.write(contents.data(), 0, num_bytes);
  FX_CHECK(status >= 0);

  buffer.vmo = std::move(vmo);
  buffer.size = num_bytes;

  fclose(fp);
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

// Implements a simple web app, which enabled keyboard events.
class WebApp : public fuchsia::ui::app::ViewProvider {
 public:
  explicit WebApp(sys::ComponentContext* component_context)
      : context_(component_context), view_provider_binding_(this) {
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

    // Load the web page.
    FX_LOGS(INFO) << "Loading web page";
    navigation_controller->LoadUrl("about:blank", fuchsia::web::LoadUrlParams(), [](auto result) {
      if (result.is_err()) {
        FX_LOGS(FATAL) << "Error while loading URL: " << static_cast<uint32_t>(result.err());
      } else {
        FX_LOGS(INFO) << "Loaded about:blank";
      }
    });
    auto web_view_config = web_view_config_lib::Config::TakeFromStartupHandle();
    FX_CHECK(!web_view_config.javascript_file().empty());
    FX_LOGS(INFO) << "Running javascript file: " << web_view_config.javascript_file();

    web_frame_->ExecuteJavaScript({"*"}, LoadFileToBuffer(web_view_config.javascript_file()),
                                  [](auto result) {
                                    if (result.is_err()) {
                                      FX_LOGS(FATAL) << "Error while executing JavaScript: "
                                                     << static_cast<uint32_t>(result.err());
                                    }
                                  });
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
    params.set_features(
        fuchsia::web::ContextFeatureFlags::VULKAN | fuchsia::web::ContextFeatureFlags::NETWORK |
        fuchsia::web::ContextFeatureFlags::AUDIO | fuchsia::web::ContextFeatureFlags::KEYBOARD);
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
  // Only Flatland
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override {
    fuchsia::web::CreateView2Args args2;
    fuchsia::ui::views::ViewCreationToken token;
    args2.set_view_creation_token(std::move(*args.mutable_view_creation_token()));
    web_frame_->CreateView2(std::move(args2));
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;
  fuchsia::web::ContextPtr web_context_;
  fuchsia::web::FramePtr web_frame_;
};
}  // namespace

int main(int argc, const char** argv) {
  auto context = sys::ComponentContext::Create();
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto web_app = std::make_unique<WebApp>(context.get());
  web_app->Run();
  context->outgoing()->ServeFromStartupInfo();
  loop.Run();
  return 0;
}
