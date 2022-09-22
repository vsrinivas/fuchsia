// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>

#include <test/virtualkeyboard/cpp/fidl.h>

#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/json_parser/json_parser.h"

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

std::string StringFromBuffer(const fuchsia::mem::Buffer& buffer) {
  size_t num_bytes = buffer.size;
  std::string str(num_bytes, 'x');
  buffer.vmo.read(str.data(), 0, num_bytes);
  return str;
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
    SetupWebEngine();
    SetupViewProvider();
  }

  void Run() {
    FX_LOGS(INFO) << "Loading web app";
    fuchsia::web::NavigationControllerPtr navigation_controller;
    NavListener navigation_event_listener;
    fidl::Binding<fuchsia::web::NavigationEventListener> navigation_event_listener_binding(
        &navigation_event_listener);
    bool is_app_loaded = false;
    web_frame_->SetNavigationEventListener(navigation_event_listener_binding.NewBinding());
    web_frame_->GetNavigationController(navigation_controller.NewRequest());
    web_frame_->SetJavaScriptLogLevel(fuchsia::web::ConsoleLogLevel::DEBUG);
    navigation_controller->LoadUrl("about:blank", fuchsia::web::LoadUrlParams(), [](auto result) {
      if (result.is_err()) {
        FX_LOGS(FATAL) << "Error while loading URL: " << static_cast<uint32_t>(result.err());
      }
    });
    web_frame_->ExecuteJavaScript({"*"}, BufferFromString(kAppCode), [&is_app_loaded](auto result) {
      if (result.is_err()) {
        FX_LOGS(FATAL) << "Error while executing JavaScript: "
                       << static_cast<uint32_t>(result.err());
      } else {
        FX_LOGS(INFO) << "App body loaded";
        is_app_loaded = true;
      }
    });
    FX_LOGS(INFO) << "Wait for app to load";
    RunLoopUntil([&] { return is_app_loaded; });

    // Plumb view to child.
    FX_LOGS(INFO) << "Waiting for view creation args from parent";

    // The client view could be attached by any of `CreateView`,
    // `CreateViewWithViewRef`, or `CreateView2`, so we need to account for all
    // three possibilities here.
    RunLoopUntil([&] { return view_token_.has_value() || create_view2_args_.has_value(); });

    if (view_token_.has_value()) {
      if (!view_ref_.has_value()) {
        auto view_ref_pair = scenic::ViewRefPair::New();
        view_ref_.emplace(std::move(view_ref_pair.view_ref));
        view_ref_control_.emplace(std::move(view_ref_pair.control_ref));
      }

      web_frame_->CreateViewWithViewRef(std::move(*view_token_), std::move(*view_ref_control_),
                                        std::move(*view_ref_));
    } else {
      // If we received `CreateView2Args`, use `CreateView2`.
      web_frame_->CreateView2(std::move(*create_view2_args_));
    }

    FX_LOGS(INFO) << "Requesting input position";
    fuchsia::web::MessagePortPtr input_position_port;
    SendMessageToWebPage(input_position_port.NewRequest(), "GET_INPUT_POSITION");

    FX_LOGS(INFO) << "Waiting for input position";
    std::optional<rapidjson::Document> input_position;
    input_position_port->ReceiveMessage([&input_position](auto web_message) {
      input_position = json::JSONParser().ParseFromString(StringFromBuffer(web_message.data()),
                                                          "web-app-response");
    });
    RunLoopUntil([&] { return input_position.has_value(); });

    // Validate structure of input position.
    FX_LOGS(INFO) << "Return input position to test fixture";
    const auto& input_pos = input_position.value();
    for (const auto& element : {"left", "right", "top", "bottom"}) {
      FX_CHECK(input_pos.HasMember(element)) << "HasMember failed for " << element;
      // Apparently sometimes these values can be floating points too.
      FX_CHECK(input_pos[element].IsNumber()) << "IsNumber failed for " << element;
    }

    // Relay position to parent.
    test::virtualkeyboard::InputPositionListenerSyncPtr position_listener_proxy;
    context_->svc()->Connect(position_listener_proxy.NewRequest());
    position_listener_proxy->Notify(test::virtualkeyboard::BoundingBox{
        .x0 = static_cast<uint32_t>(input_pos["left"].GetFloat()),
        .y0 = static_cast<uint32_t>(input_pos["top"].GetFloat()),
        .x1 = static_cast<uint32_t>(input_pos["right"].GetFloat()),
        .y1 = static_cast<uint32_t>(input_pos["bottom"].GetFloat())});

    loop_.Run();
  }

 private:
  static constexpr auto kAppCode = R"JS(
    console.info('injecting body');
    // Create a page with a single input box.
    // * When the user taps inside the input box (and the keyboard is currently hidden),
    //   web-engine should request the virtual keyboard be made visible.
    // * When the user taps outside the input box (and the keyboard is currently visible),
    //   web-engine should request the virtual keyboard me made hidden.
    document.write('<html><body><input id="textbox" /></body></html>');
    document.body.style.backgroundColor='#ff00ff';
    document.body.onclick = function(event) {
      document.body.style.backgroundColor='#40e0d0';
      let touch_event = JSON.stringify({
        x: event.screenX,
        y: event.screenY,
      });
      console.info('Got touch event ', touch_event);
    };
    function receiveMessage(event) {
      if (event.data == "GET_INPUT_POSITION") {
        let message = JSON.stringify(document.getElementById('textbox').getBoundingClientRect());
        console.info('sending input position ', message);
        event.ports[0].postMessage(message);
      } else {
        console.error('ignoring unexpected message: ' + event.data);
      }
    };
    window.addEventListener('message', receiveMessage, false);
    )JS";

  void SetupWebEngine() {
    auto web_context_provider = context_->svc()->Connect<fuchsia::web::ContextProvider>();
    auto incoming_service_clone = context_->svc()->CloneChannel();
    web_context_provider.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "web_context_provider: " << zx_status_get_string(status);
    });
    FX_CHECK(incoming_service_clone.is_valid());

    fuchsia::web::CreateContextParams params;
    params.set_service_directory(std::move(incoming_service_clone));
    params.set_features(fuchsia::web::ContextFeatureFlags::KEYBOARD |
                        fuchsia::web::ContextFeatureFlags::VIRTUAL_KEYBOARD);
    web_context_provider->Create(std::move(params), web_context_.NewRequest());
    web_context_.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "web_context_: " << zx_status_get_string(status);
    });
    web_context_->CreateFrame(web_frame_.NewRequest());
    web_frame_.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "web_frame_: " << zx_status_get_string(status);
    });
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
    view_token_ = scenic::ToViewToken(std::move(token));
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override {
    view_token_ = scenic::ToViewToken(std::move(token));
    view_ref_control_ = std::move(view_ref_control);
    view_ref_ = std::move(view_ref);
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override {
    fuchsia::ui::views::ViewCreationToken token;
    create_view2_args_.emplace();
    create_view2_args_->set_view_creation_token(std::move(*args.mutable_view_creation_token()));
  }

  void SendMessageToWebPage(fidl::InterfaceRequest<fuchsia::web::MessagePort> message_port,
                            const std::string& message) {
    fuchsia::web::WebMessage web_message;
    web_message.set_data(BufferFromString(message));

    std::vector<fuchsia::web::OutgoingTransferable> outgoing;
    outgoing.emplace_back(
        fuchsia::web::OutgoingTransferable::WithMessagePort(std::move(message_port)));
    web_message.set_outgoing_transfer(std::move(outgoing));

    web_frame_->PostMessage(/*target_origin=*/"*", std::move(web_message),
                            [](auto result) { FX_CHECK(!result.is_err()); });
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

  // CreateView / CreateViewWithViewRef args.
  std::optional<fuchsia::ui::views::ViewToken> view_token_;
  std::optional<fuchsia::ui::views::ViewRef> view_ref_;
  std::optional<fuchsia::ui::views::ViewRefControl> view_ref_control_;

  // CreateView2 args.
  std::optional<fuchsia::web::CreateView2Args> create_view2_args_;
};
}  // namespace

int main(int argc, const char** argv) { WebApp().Run(); }
