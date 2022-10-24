// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/test/input/cpp/fidl.h>
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

std::vector<fuchsia::ui::test::input::MouseButton> GetPressedButtons(int buttons) {
  std::vector<fuchsia::ui::test::input::MouseButton> pressed_buttons;

  if (buttons & 0x1) {
    pressed_buttons.push_back(fuchsia::ui::test::input::MouseButton::FIRST);
  }
  if (buttons & 0x1 >> 1) {
    pressed_buttons.push_back(fuchsia::ui::test::input::MouseButton::SECOND);
  }
  if (buttons & 0x1 >> 2) {
    pressed_buttons.push_back(fuchsia::ui::test::input::MouseButton::THIRD);
  }

  return pressed_buttons;
}

fuchsia::ui::test::input::MouseEventPhase GetPhase(std::string type) {
  if (type == "add") {
    return fuchsia::ui::test::input::MouseEventPhase::ADD;
  } else if (type == "hover") {
    return fuchsia::ui::test::input::MouseEventPhase::HOVER;
  } else if (type == "mousedown") {
    return fuchsia::ui::test::input::MouseEventPhase::DOWN;
  } else if (type == "mousemove") {
    return fuchsia::ui::test::input::MouseEventPhase::MOVE;
  } else if (type == "mouseup") {
    return fuchsia::ui::test::input::MouseEventPhase::UP;
  } else if (type == "wheel") {
    return fuchsia::ui::test::input::MouseEventPhase::WHEEL;
  } else {
    FX_LOGS(FATAL) << "Invalid mouse event type: " << type;
    exit(1);
  }
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
    bool window_resized = false;
    SendMessageToWebPage(message_port.NewRequest(), "REGISTER_PORT");
    message_port->ReceiveMessage([&is_port_registered, &window_resized](auto web_message) {
      auto message = StringFromBuffer(web_message.data());
      // JS already saw window has size, don't wait for resize.
      if (message == "PORT_REGISTERED WINDOW_RESIZED") {
        window_resized = true;
      } else {
        FX_CHECK(message == "PORT_REGISTERED") << "Expected PORT_REGISTERED but got " << message;
      }
      is_port_registered = true;
    });
    RunLoopUntil([&] { return is_port_registered; });

    if (!window_resized) {
      RunLoopUntil(
          [&navigation_event_listener] { return navigation_event_listener.window_resized_; });
    }

    fuchsia::ui::test::input::MouseInputListenerSyncPtr mouse_input_listener;
    context_->svc()->Connect(mouse_input_listener.NewRequest());

    RunLoopForMouseReponse(mouse_input_listener, message_port);
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
        wheel_h: event.deltaX,
        wheel_v: event.deltaY,
        device_scale_factor: window.devicePixelRatio,
        buttons: event.buttons
      });
      console.info('Reporting mouse up event ', response);
      port.postMessage(response);
    };
    document.body.onwheel = function(event) {
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
      console.info('Reporting mouse wheel event ', response);
      port.postMessage(response);
    };
    window.onresize = function(event) {
      if (window.innerWidth != 0) {
        console.info('size: ', window.innerWidth, window.innerHeight);
        document.title = 'window_resized';
      }
    };
    function receiveMessage(event) {
      if (event.data == "REGISTER_PORT") {
        console.log("received REGISTER_PORT");
        port = event.ports[0];
        if (window.innerWidth != 0) {
          port.postMessage('PORT_REGISTERED WINDOW_RESIZED');
        } else {
          port.postMessage('PORT_REGISTERED');
        }
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
      FX_LOGS(WARNING) << "web_context_provider: " << zx_status_get_string(status);
    });
    FX_CHECK(incoming_service_clone.is_valid());

    fuchsia::web::CreateContextParams params;
    params.set_service_directory(std::move(incoming_service_clone));
    // Enable Vulkan to allow WebEngine run on Flatland.
    params.set_features(fuchsia::web::ContextFeatureFlags::VULKAN);
    web_context_provider->Create(std::move(params), web_context_.NewRequest());
    web_context_.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "web_context_: " << zx_status_get_string(status);
    });
    web_context_->CreateFrame(web_frame_.NewRequest());
    web_frame_.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "web_frame_: " << zx_status_get_string(status);
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

  void RunLoopForMouseReponse(
      fuchsia::ui::test::input::MouseInputListenerSyncPtr& mouse_input_listener,
      fuchsia::web::MessagePortPtr& message_port) {
    while (true) {
      bool got_mouse_event = false;
      FX_LOGS(INFO) << "Waiting for mouse response message";

      message_port->ReceiveMessage([&mouse_input_listener, &got_mouse_event](auto web_message) {
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
        fuchsia::ui::test::input::MouseInputListenerReportMouseInputRequest request;
        request.set_time_received(mouse_resp["epoch_msec"].GetInt64() * 1000 * 1000);
        request.set_local_x(mouse_resp["x"].GetDouble());
        request.set_local_y(mouse_resp["y"].GetDouble());
        request.set_device_pixel_ratio(mouse_resp["device_scale_factor"].GetDouble());
        request.set_phase(GetPhase(mouse_resp["type"].GetString()));
        request.set_buttons(GetPressedButtons(mouse_resp["buttons"].GetInt()));

        if (mouse_resp.HasMember("wheel_h")) {
          FX_CHECK(mouse_resp["wheel_h"].IsNumber());
          request.set_wheel_x_physical_pixel(mouse_resp["wheel_h"].GetDouble());
        }
        if (mouse_resp.HasMember("wheel_v")) {
          FX_CHECK(mouse_resp["wheel_v"].IsNumber());
          request.set_wheel_y_physical_pixel(mouse_resp["wheel_v"].GetDouble());
        }

        request.set_component_name("mouse-input-chromium");

        FX_LOGS(INFO) << "Got mouse response message " << mouse_resp["type"].GetString();

        mouse_input_listener->ReportMouseInput(std::move(request));

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
