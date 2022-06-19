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
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>

#include <test/mouse/cpp/fidl.h>

#include "fuchsia/ui/views/cpp/fidl.h"
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
    if (nav_state.has_is_main_document_loaded()) {
      FX_LOGS(INFO) << "nav_state.is_main_document_loaded = "
                    << nav_state.is_main_document_loaded();
      is_main_document_loaded_ = nav_state.is_main_document_loaded();
    }
    if (nav_state.has_title()) {
      FX_LOGS(INFO) << "nav_state.title = " << nav_state.title();
      if (nav_state.title() == "about:blank") {
        loaded_about_blank_ = true;
      }
      if (nav_state.title() == "window_resized") {
        window_resized_ = true;
      }
    }

    send_ack();
  }
  bool loaded_about_blank_ = false;
  bool is_main_document_loaded_ = false;
  bool window_resized_ = false;
};

// Implements a simple web app, which responds to mouse events.
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
    web_frame_->SetNavigationEventListener(navigation_event_listener_binding.NewBinding());

    web_frame_->GetNavigationController(navigation_controller.NewRequest());
    navigation_controller->LoadUrl("about:blank", fuchsia::web::LoadUrlParams(), [](auto result) {
      if (result.is_err()) {
        FX_LOGS(FATAL) << "Error while loading URL: " << static_cast<uint32_t>(result.err());
      }
    });

    // Wait for navigation loaded "about:blank" page then inject JS code, to avoid inject JS to
    // wrong page.
    RunLoopUntil([&navigation_event_listener] {
      return navigation_event_listener.loaded_about_blank_ &&
             navigation_event_listener.is_main_document_loaded_;
    });

    bool is_js_loaded = false;
    web_frame_->ExecuteJavaScript({"*"}, BufferFromString(kAppCode), [&is_js_loaded](auto result) {
      if (result.is_err()) {
        FX_LOGS(FATAL) << "Error while executing JavaScript: "
                       << static_cast<uint32_t>(result.err());
      } else {
        is_js_loaded = true;
      }
    });

    RunLoopUntil([&] { return is_js_loaded; });

    fuchsia::web::MessagePortPtr message_port;
    bool is_port_registered = false;
    SendMessageToWebPage(message_port.NewRequest(), "REGISTER_PORT");
    message_port->ReceiveMessage([&is_port_registered](auto web_message) {
      auto message = StringFromBuffer(web_message.data());
      FX_CHECK(message == "PORT_REGISTERED") << "Expected PORT_REGISTERED but got " << message;
      is_port_registered = true;
    });
    RunLoopUntil([&] { return is_port_registered; });

    RunLoopUntil(
        [&navigation_event_listener] { return navigation_event_listener.window_resized_; });

    test::mouse::ResponseListenerSyncPtr response_listener_proxy;
    context_->svc()->Connect(response_listener_proxy.NewRequest());

    response_listener_proxy->NotifyWebEngineReady();

    RunLoopForMouseReponse(response_listener_proxy, message_port);
  }

 private:
  static constexpr auto kAppCode = R"JS(
    let port;
    document.body.onmousemove = function(event) {
      console.assert(port != null);
      let response = JSON.stringify({
        type: event.type,
        epoch_msec: Date.now(),
        x: event.clientX,
        y: event.clientY,
        wheel_h: event.deltaX,
        wheel_v: event.deltaY,
        device_scale_factor: window.devicePixelRatio,
        buttons: event.buttons
      });
      console.info('Reporting mouse move event ', response);
      port.postMessage(response);
    };
    document.body.onmousedown = function(event) {
      console.assert(port != null);
      let response = JSON.stringify({
        type: event.type,
        epoch_msec: Date.now(),
        x: event.clientX,
        y: event.clientY,
        wheel_h: event.deltaX,
        wheel_v: event.deltaY,
        device_scale_factor: window.devicePixelRatio,
        buttons: event.buttons
      });
      console.info('Reporting mouse down event ', response);
      port.postMessage(response);
    };
    document.body.onmouseup = function(event) {
      console.assert(port != null);
      let response = JSON.stringify({
        type: event.type,
        epoch_msec: Date.now(),
        x: event.clientX,
        y: event.clientY,
        device_scale_factor: window.devicePixelRatio,
        buttons: event.buttons
      });
      console.info('Reporting mouse up event ', response);
      port.postMessage(response);
    };
    window.onresize = function(event) {
      if (window.innerWidth != 0) {
        console.info('size: ', window.innerWidth, window.innerHeight);
        document.title = 'window_resized';
      }
    }
    function receiveMessage(event) {
      if (event.data == "REGISTER_PORT") {
        console.log("received REGISTER_PORT");
        port = event.ports[0];
        port.postMessage('PORT_REGISTERED');
      } else {
        console.error('received unexpected message: ' + event.data);
      }
    };
    window.addEventListener('message', receiveMessage, false);
    console.info('JS loaded');
  )JS";

  void SetupWebEngine() {
    auto web_context_provider = context_->svc()->Connect<fuchsia::web::ContextProvider>();
    auto incoming_service_clone = context_->svc()->CloneChannel();
    web_context_provider.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "web_context_provider: " << zx_status_get_string(status);
    });
    FX_CHECK(incoming_service_clone.is_valid());

    fuchsia::web::CreateContextParams params;
    params.set_service_directory(std::move(incoming_service_clone));
    // Enable Vulkan to allow WebEngine run on Flatland.
    params.set_features(fuchsia::web::ContextFeatureFlags::VULKAN);
    web_context_provider->Create(std::move(params), web_context_.NewRequest());
    web_context_.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "web_context_: " << zx_status_get_string(status);
    });
    web_context_->CreateFrame(web_frame_.NewRequest());
    web_frame_.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "web_frame_: " << zx_status_get_string(status);
    });

    // Setup log level in JS to get logs.
    web_frame_->SetJavaScriptLogLevel(fuchsia::web::ConsoleLogLevel::INFO);
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
    // Flatland only use |CreateView2|.
    FX_LOGS(FATAL) << "CreateView() is not implemented.";
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override {
    // Flatland only use |CreateView2|.
    FX_LOGS(FATAL) << "CreateViewWithViewRef() is not implemented.";
  }

  void CreateView2(fuchsia::ui::app::CreateView2Args args) override {
    fuchsia::web::CreateView2Args args2;
    fuchsia::ui::views::ViewCreationToken token;
    args2.set_view_creation_token(std::move(*args.mutable_view_creation_token()));
    web_frame_->CreateView2(std::move(args2));
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

  void RunLoopForMouseReponse(test::mouse::ResponseListenerSyncPtr& response_listener_proxy,
                              fuchsia::web::MessagePortPtr& message_port) {
    while (true) {
      bool got_mouse_event = false;
      FX_LOGS(INFO) << "Waiting for mouse response message";

      message_port->ReceiveMessage([&response_listener_proxy, &got_mouse_event](auto web_message) {
        std::optional<rapidjson::Document> mouse_response;
        mouse_response = json::JSONParser().ParseFromString(StringFromBuffer(web_message.data()),
                                                            "web-app-response");
        // Validate structure of mouse response.
        const auto& mouse_resp = mouse_response.value();
        FX_CHECK(mouse_resp.HasMember("type"));
        FX_CHECK(mouse_resp.HasMember("epoch_msec"));
        FX_CHECK(mouse_resp.HasMember("x"));
        FX_CHECK(mouse_resp.HasMember("y"));
        FX_CHECK(mouse_resp.HasMember("device_scale_factor"));
        FX_CHECK(mouse_resp.HasMember("buttons"));
        FX_CHECK(mouse_resp["type"].IsString());
        FX_CHECK(mouse_resp["epoch_msec"].IsInt64());
        FX_CHECK(mouse_resp["x"].IsNumber());
        FX_CHECK(mouse_resp["y"].IsNumber());
        FX_CHECK(mouse_resp["device_scale_factor"].IsNumber());
        FX_CHECK(mouse_resp["buttons"].IsNumber());

        // Relay response to parent.
        test::mouse::PointerData pointer_data;
        pointer_data.set_time_received(mouse_resp["epoch_msec"].GetInt64() * 1000 * 1000);
        pointer_data.set_local_x(mouse_resp["x"].GetDouble());
        pointer_data.set_local_y(mouse_resp["y"].GetDouble());
        pointer_data.set_device_scale_factor(mouse_resp["device_scale_factor"].GetDouble());
        pointer_data.set_type(mouse_resp["type"].GetString());
        pointer_data.set_buttons(mouse_resp["buttons"].GetInt());

        pointer_data.set_component_name("mouse-input-chromium");

        FX_LOGS(INFO) << "Got mouse response message " << pointer_data.type();

        response_listener_proxy->Respond(std::move(pointer_data));

        got_mouse_event = true;
      });
      RunLoopUntil([&]() { return got_mouse_event; });
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
