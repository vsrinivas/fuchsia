// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/test/ui/cpp/fidl.h>
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
        is_app_loaded = true;
      }
    });
    RunLoopUntil([&] { return is_app_loaded; });

    FX_LOGS(INFO) << "Registering message-port with web app";
    fuchsia::web::MessagePortPtr message_port;
    bool is_port_registered = false;
    SendMessageToWebPage(message_port.NewRequest(), "REGISTER_PORT");
    message_port->ReceiveMessage([&is_port_registered](auto web_message) {
      auto message = StringFromBuffer(web_message.data());
      FX_CHECK(message == "PORT_REGISTERED") << "Expected PORT_REGISTERED but got " << message;
      is_port_registered = true;
    });
    RunLoopUntil([&] { return is_port_registered; });

    // Plumb view to child.
    //
    // Note well: our parent will inject a touch event immediately after we plumb the view
    // to the child. Hence, we must not call `CreateView()` before `is_port_registered`
    // is true.
    FX_LOGS(INFO) << "Waiting for view token from parent";
    RunLoopUntil([&] { return view_token_.value.is_valid(); });
    web_frame_->CreateView(std::move(view_token_));

    FX_LOGS(INFO) << "Waiting for tap response message";
    std::optional<rapidjson::Document> tap_response;
    message_port->ReceiveMessage([&tap_response](auto web_message) {
      tap_response = json::JSONParser().ParseFromString(StringFromBuffer(web_message.data()),
                                                        "web-app-response");
    });
    RunLoopUntil([&] { return tap_response.has_value(); });

    // Validate structure of touch response.
    const auto& tap_resp = tap_response.value();
    FX_CHECK(tap_resp.HasMember("epoch_msec"));
    FX_CHECK(tap_resp.HasMember("x"));
    FX_CHECK(tap_resp.HasMember("y"));
    FX_CHECK(tap_resp.HasMember("device_pixel_ratio"));
    FX_CHECK(tap_resp["epoch_msec"].IsInt64());
    FX_CHECK(tap_resp["x"].IsInt());
    FX_CHECK(tap_resp["y"].IsInt());
    FX_CHECK(tap_resp["device_pixel_ratio"].IsDouble());

    // Relay response to parent.
    fuchsia::test::ui::ResponseListenerSyncPtr response_listener_proxy;
    fuchsia::test::ui::PointerData pointer_data;
    context_->svc()->Connect(response_listener_proxy.NewRequest());
    pointer_data.set_time_received(tap_resp["epoch_msec"].GetInt64() * 1000 * 1000);
    pointer_data.set_local_x(tap_resp["x"].GetInt());
    pointer_data.set_local_y(tap_resp["y"].GetInt());
    pointer_data.set_device_pixel_ratio(tap_resp["device_pixel_ratio"].GetDouble());
    response_listener_proxy->Respond(std::move(pointer_data));
  }

 private:
  static constexpr auto kAppCode = R"JS(
    let port;
    document.body.style.backgroundColor='#ff00ff';
    document.body.onclick = function(event) {
      document.body.style.backgroundColor='#40e0d0';
      console.assert(port != null);
      let response = JSON.stringify({
        epoch_msec: Date.now(),
        x: event.screenX,
        y: event.screenY,
        device_pixel_ratio: window.devicePixelRatio,
      });
      console.info('Reporting touch event ', response);
      port.postMessage(response);
    };
    function receiveMessage(event) {
      if (event.data == "REGISTER_PORT") {
        port = event.ports[0];
        port.postMessage('PORT_REGISTERED');
      } else {
        console.error('received unexpected message: ' + event.data);
      }
    };
    window.addEventListener('message', receiveMessage, false);
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
    web_context_provider->Create(std::move(params), web_context_.NewRequest());
    web_context_.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "web_context_: " << zx_status_get_string(status);
    });
    web_context_->CreateFrame(web_frame_.NewRequest());
    web_frame_.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "web_frame_: " << zx_status_get_string(status);
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
    // Save the token until we're ready to use it. (We may receive the token before we've
    // finished setting up the web app.)
    view_token_ = scenic::ToViewToken(std::move(token));
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
  fuchsia::ui::views::ViewToken view_token_;
  fuchsia::web::ContextPtr web_context_;
  fuchsia::web::FramePtr web_frame_;
};
}  // namespace

int main(int argc, const char** argv) { WebApp().Run(); }
