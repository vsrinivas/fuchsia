// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The contents of this web application are heavily borrowed from prior work
// such as mouse-input-chromium.cc, virtual-keyboard-test-ip.cc and other
// similar efforts.

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
#include <unistd.h>
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

// Listens to navigation events forwarded from the web page into this web app.
// The navigation events are used for simplistic communication about the
// web page's lifecycle through modifying the title of the displayed page.
// Modifying the title is used for boolean events, while anything that
// requires more complex communication uses a port.
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
      if (nav_state.title() == "text_input_focused") {
        text_input_focused_ = true;
      }
    }

    send_ack();
  }
  bool loaded_about_blank_ = false;
  bool is_main_document_loaded_ = false;
  bool window_resized_ = false;
  bool text_input_focused_ = false;
};

// Implements a simple web app, which responds to tap and keyboard events.
class WebApp : public fuchsia::ui::app::ViewProvider {
 public:
  WebApp()
      : loop_(&kAsyncLoopConfigAttachToCurrentThread),
        context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        view_provider_binding_(this) {
    SetUpViewProvider();
    SetUpWebEngine();
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

    // Wait for navigation loaded "about:blank" page then inject JS code, to avoid injecting JS to
    // the wrong page.
    RunLoopUntil([&navigation_event_listener] {
      return navigation_event_listener.loaded_about_blank_ &&
             navigation_event_listener.is_main_document_loaded_;
    });

    // Load the javascript which sets up the test HTML page. The test HTML page is instrumented
    // with event handlers that know how to report back to the web app.
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

    // Register a port for web communication.
    fuchsia::web::MessagePortPtr message_port;
    bool is_port_registered = false;
    SendMessageToWebPage(message_port.NewRequest(), "REGISTER_PORT");
    message_port->ReceiveMessage([&is_port_registered](auto web_message) {
      auto message = StringFromBuffer(web_message.data());
      FX_CHECK(message == "PORT_REGISTERED") << "Expected PORT_REGISTERED but got " << message;
      is_port_registered = true;
    });

    // Wait until various lifecycle stages in the web engine are reached before proceeding.
    FX_LOGS(INFO) << "Wait for is_port_registered";
    RunLoopUntil([&] { return is_port_registered; });
    FX_LOGS(INFO) << "Wait for window_resized";
    RunLoopUntil(
        [&navigation_event_listener] { return navigation_event_listener.window_resized_; });
    FX_LOGS(INFO) << "Wait for text_input_focused";
    RunLoopUntil(
        [&navigation_event_listener] { return navigation_event_listener.text_input_focused_; });

    // Send `ReportReady` to the test fixture, and wait until the call is
    // acknowledged.
    fuchsia::ui::test::input::KeyboardInputListenerPtr response_listener_proxy;
    context_->svc()->Connect(response_listener_proxy.NewRequest());
    bool report_ready_acked = false;
    response_listener_proxy->ReportReady([&report_ready_acked]() { report_ready_acked = true; });
    FX_LOGS(INFO) << "Wait for report_ready_acked";
    RunLoopUntil([&report_ready_acked] { return report_ready_acked; });

    // Watch for any changes in the text area, and forward repeatedly to the
    // response listener in the test fixture.
    for (;;) {
      // This WebMessage comes from the Javascript code (below).
      std::optional<fuchsia::web::WebMessage> received;
      message_port->ReceiveMessage(
          [&received](fuchsia::web::WebMessage web_message) { received = std::move(web_message); });
      RunLoopUntil([&received] { return received.has_value(); });

      // Forward the message to the test fixture.
      auto str = StringFromBuffer(received.value().data());
      fuchsia::ui::test::input::KeyboardInputListenerReportTextInputRequest request;
      request.set_text(std::move(str));
      response_listener_proxy->ReportTextInput(std::move(request));
    }
  }

 private:
  // The application code that will be loaded up.
  static constexpr auto kAppCode = R"JS(
    let port;

    // Report a window resize event by changing the document title.
    window.onresize = function(event) {
      if (window.innerWidth != 0) {
        console.info('size: ', window.innerWidth, window.innerHeight);
        document.title = 'window_resized';
      }
    };

    // Registers a port for sending messages between the web engine and this
    // web app.
    function receiveMessage(event) {
      if (event.data == "REGISTER_PORT") {
        console.log("received REGISTER_PORT");
        port = event.ports[0];
        port.postMessage('PORT_REGISTERED');
      } else {
        console.error('received unexpected message: ' + event.data);
      }
    };

    function sendMessageEvent(messageObj) {
      let message = JSON.stringify(messageObj);
      port.postMessage(message);
    }

    const headHtml = `
    <style>
      body {
        height: 100%;
        background-color: #000077; /* dark blue */
        color: white;
      }
      #text-input {
        height: 100%;
        width: 100%;
        background-color: #ca2c92; /* royal fuchsia */
        font-size: 36pt;
      }
    </style>
    `;

    // Installs a large text field. The text field occupies most of the
    // screen for easy navigation.
    const bodyHtml = `
    <p id='some-text'>Some text below:</p>
    <textarea id="text-input" rows="3" cols="20"></textarea>
    `;

    document.head.innerHTML += headHtml;
    document.body.innerHTML = bodyHtml;

    /** @type HTMLInputElement */
    let $input = document.querySelector("#text-input");

    // Every time a keyup event happens on input, relay the key to the web app.
    // "keyup" is selected instead of "keydown" because "keydown" will show us
    // the *previous* state of the text area.
    $input.addEventListener("keyup", function (e) {
      sendMessageEvent({
        text: $input.value,
      });
    });

    // Sends a signal that the text area is focused, when that happens. The
    // easiest way to do that is to change the document title. There is a
    // navigation listener which will get notified of the title change.
    $input.addEventListener('focus', function (e) {
      document.title = 'text_input_focused';
    });

    window.addEventListener('message', receiveMessage, false);
    console.info('JS loaded');
  )JS";

  void SetUpWebEngine() {
    auto web_context_provider = context_->svc()->Connect<fuchsia::web::ContextProvider>();
    auto incoming_service_clone = context_->svc()->CloneChannel();
    web_context_provider.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "web_context_provider: " << zx_status_get_string(status);
    });
    FX_CHECK(incoming_service_clone.is_valid());

    fuchsia::web::CreateContextParams params;
    // Enables chrome remote devtools if needed.
    // params.set_remote_debugging_port(12342);
    params.set_service_directory(std::move(incoming_service_clone));
    // Enable Vulkan to allow WebEngine run on Flatland. Also, enable
    // keyboard events.
    params.set_features(fuchsia::web::ContextFeatureFlags::VULKAN |
                        fuchsia::web::ContextFeatureFlags::NETWORK |
                        fuchsia::web::ContextFeatureFlags::KEYBOARD);
    auto web_context_request = web_context_.NewRequest();
    web_context_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "web_context_ error: " << zx_status_get_string(status);
    });
    web_context_provider->Create(std::move(params), std::move(web_context_request));
    fuchsia::web::CreateFrameParams frame_params;
    frame_params.set_debug_name("text-input-chromium");
    // frame_params.set_enable_remote_debugging(false);
    auto web_frame_request = web_frame_.NewRequest();
    web_frame_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "web_frame_ error: " << zx_status_get_string(status);
    });
    web_context_->CreateFrameWithParams(std::move(frame_params), std::move(web_frame_request));

    // Setup log level in JS to get logs.
    web_frame_->SetJavaScriptLogLevel(fuchsia::web::ConsoleLogLevel::DEBUG);
  }

  void SetUpViewProvider() {
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

  // This is a GFX API call, not a flatland call, so it is not implemented.
  // This test does not work under GFX.
  //
  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair, fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) override {
    FX_LOGS(FATAL) << "CreateView() is not implemented.";
  }

  // This is a GFX API call, not a flatland call, so it is not implemented.
  // This test does not work under GFX.
  //
  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair, fuchsia::ui::views::ViewRefControl,
                             fuchsia::ui::views::ViewRef) override {
    FX_LOGS(FATAL) << "CreateViewWithViewRef() is not implemented.";
  }

  // This API call is a Flatland API call, so we must implement it to create
  // a Flatland view.
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override {
    fuchsia::web::CreateView2Args args2;
    fuchsia::ui::views::ViewCreationToken token;
    args2.set_view_creation_token(std::move(*args.mutable_view_creation_token()));
    web_frame_->CreateView2(std::move(args2));
    FX_LOGS(DEBUG) << "View created";
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
};

}  // namespace

int main(int argc, const char** argv) { WebApp().Run(); }
