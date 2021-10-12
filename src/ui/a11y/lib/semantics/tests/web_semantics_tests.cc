// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/realm_builder.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <cctype>

#include <gtest/gtest.h>

#include "src/chromium/web_runner_tests/mock_get.h"
#include "src/chromium/web_runner_tests/test_server.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture_v2.h"
#include "src/ui/testing/views/embedder_view.h"

namespace accessibility_test {
namespace {

using sys::testing::AboveRoot;
using sys::testing::CapabilityRoute;
using sys::testing::Component;
using sys::testing::LegacyComponentUrl;
using sys::testing::Mock;
using sys::testing::Moniker;
using sys::testing::Protocol;

static constexpr auto kStaticHtml = R"(
<html>
  <head>
    <title>Say something. Anything.</title>
  </head>
  <body>
    <p hidden>Anything but... that.</p>
    <button type="button">Click here</button>
  </body>
</html>)";

static constexpr auto kDynamicHtml = R"(
<html>
  <head>
    <title>Dynamic test</title>
  </head>
  <body>
    <script>
      function incrementCounter() {
        const counter = document.querySelector('#counter');
        counter.textContent = Number.parseInt(counter.textContent, 10) + 1;
      }
    </script>
    The button has been clicked <span id="counter">0</span> times.
    <button type="button" onclick='incrementCounter()'>Increment</button>
  </body>
</html>
)";

static constexpr auto kBigListHtml = R"(
<html>
  <head>
    <title>Big list test</title>
  </head>
  <body onLoad='generateList()'>
    <script>
      function generateList() {
        const list = document.querySelector("#list");
        const template = document.querySelector('#entry');
        for (let i = 0; i < 1000; i++) {
          const clone = template.content.cloneNode(true);
          const li = clone.querySelector("li");
          li.textContent = "Entry " + i;
          list.appendChild(clone);
        }
      }
    </script>
    <ul id="list"/>
    <template id="entry">
      <li/>
    </template>
  </body>
</html>
)";

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

// Mock component that will own a web view and route ViewProvider requests through it.
class WebViewProxy : public fuchsia::ui::app::ViewProvider, public MockComponent {
 public:
  explicit WebViewProxy(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  ~WebViewProxy() override = default;

  void Start(std::unique_ptr<MockHandles> mock_handles) override {
    // Connect to services in the realm.
    auto svc = mock_handles->svc();
    auto web_context_provider = svc.Connect<fuchsia::web::ContextProvider>();
    auto incoming_service_clone = svc.CloneChannel();
    web_context_provider.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "web_context_provider: " << zx_status_get_string(status);
    });
    FX_CHECK(incoming_service_clone.is_valid());

    // Set up web context.
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

    // Load the web page.
    FX_LOGS(INFO) << "Loading web page";
    fuchsia::web::NavigationControllerPtr navigation_controller;
    NavListener navigation_event_listener;
    fidl::Binding<fuchsia::web::NavigationEventListener> navigation_event_listener_binding(
        &navigation_event_listener);
    bool is_web_page_loaded = false;
    web_frame_->SetNavigationEventListener(navigation_event_listener_binding.NewBinding());
    web_frame_->GetNavigationController(navigation_controller.NewRequest());
    navigation_controller->LoadUrl("about:blank", fuchsia::web::LoadUrlParams(), [](auto result) {
      if (result.is_err()) {
        FX_LOGS(FATAL) << "Error while loading URL: " << static_cast<uint32_t>(result.err());
      } else {
        FX_LOGS(INFO) << "Loaded web page";
      }
    });

    // Publish the ViewProvider service.
    FX_CHECK(
        mock_handles->outgoing()->AddPublicService(
            fidl::InterfaceRequestHandler<fuchsia::ui::app::ViewProvider>([this](auto request) {
              bindings_.AddBinding(this, std::move(request), dispatcher_);
            })) == ZX_OK);
    mock_handles_.emplace_back(std::move(mock_handles));
  }

  void LoadHtml(std::string html) {
    web_frame_->ExecuteJavaScript(
        {"*"}, BufferFromString(fxl::StringPrintf("document.write(`%s`);", html.c_str())),
        [](auto result) {
          if (result.is_err()) {
            FX_LOGS(FATAL) << "Error while executing JavaScript: "
                           << static_cast<uint32_t>(result.err());
          } else {
            FX_LOGS(INFO) << "Web page is loaded";
          }
        });
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override {
    web_frame_->CreateViewWithViewRef(scenic::ToViewToken(std::move(token)),
                                      std::move(view_ref_control), std::move(view_ref));
  }

  // |fuchsia.ui.app.ViewProvider|
  void CreateView(zx::eventpair view_handle, fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) override {
    FX_LOGS(ERROR) << "CreateView() is not implemented.";
  }

  // |fuchsia.ui.app.ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override {
    FX_LOGS(ERROR) << "CreateView2() is not implemented.";
  }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::vector<std::unique_ptr<MockHandles>> mock_handles_{};
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;
  fuchsia::ui::views::ViewToken view_token_;
  fuchsia::web::ContextPtr web_context_;
  fuchsia::web::FramePtr web_frame_;
};

class WebSemanticsTest : public SemanticsIntegrationTestV2 {
 public:
  static constexpr auto kWebViewMoniker = Moniker{"web_view"};

  static constexpr auto kFontsProvider = Moniker{"fonts_provider"};
  static constexpr auto kFontsProviderUrl =
      LegacyComponentUrl{"fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx"};

  static constexpr auto kTextManager = Moniker{"text_manager"};
  static constexpr auto kTextManagerUrl =
      LegacyComponentUrl{"fuchsia-pkg://fuchsia.com/text_manager#meta/text_manager.cmx"};

  static constexpr auto kIntl = Moniker{"intl"};
  static constexpr auto kIntlUrl = LegacyComponentUrl{
      "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager.cmx"};

  static constexpr auto kMemoryPressureProvider = Moniker{"memory_pressure_provider"};
  static constexpr auto kMemoryPressureProviderUrl =
      LegacyComponentUrl{"fuchsia-pkg://fuchsia.com/memory_monitor#meta/memory_monitor.cmx"};

  static constexpr auto kNetstack = Moniker{"netstack"};
  static constexpr auto kNetstackUrl =
      LegacyComponentUrl{"fuchsia-pkg://fuchsia.com/semantics-integration-tests#meta/netstack.cmx"};

  static constexpr auto kWebContextProvider = Moniker{"web_context_provider"};
  static constexpr auto kWebContextProviderUrl =
      LegacyComponentUrl{"fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx"};

  WebSemanticsTest() = default;
  ~WebSemanticsTest() override = default;

  WebViewProxy* web_view_proxy() const { return web_view_proxy_.get(); }

  std::vector<std::pair<Moniker, Component>> GetTestComponents() override {
    return {
        std::make_pair(kWebViewMoniker, Component{.source = Mock{web_view_proxy()}}),
        std::make_pair(kFontsProvider, Component{.source = kFontsProviderUrl}),
        std::make_pair(kTextManager, Component{.source = kTextManagerUrl}),
        std::make_pair(kIntl, Component{.source = kIntlUrl}),
        std::make_pair(kMemoryPressureProvider, Component{.source = kMemoryPressureProviderUrl}),
        std::make_pair(kNetstack, Component{.source = kNetstackUrl}),
        std::make_pair(kWebContextProvider, Component{.source = kWebContextProviderUrl}),
    };
  }

  std::vector<CapabilityRoute> GetTestRoutes() override {
    return {{.capability = Protocol{fuchsia::fonts::Provider::Name_},
             .source = kFontsProvider,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::ui::input::ImeService::Name_},
             .source = kTextManager,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::ui::input::ImeVisibilityService::Name_},
             .source = kTextManager,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::intl::PropertyProvider::Name_},
             .source = kIntl,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::memorypressure::Provider::Name_},
             .source = kMemoryPressureProvider,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::netstack::Netstack::Name_},
             .source = kNetstack,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::net::interfaces::State::Name_},
             .source = kNetstack,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_},
             .source = kSemanticsManagerMoniker,
             .targets = {kWebViewMoniker, kWebContextProvider}},
            {.capability = Protocol{fuchsia::web::ContextProvider::Name_},
             .source = kWebContextProvider,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::sys::Environment::Name_},
             .source = AboveRoot(),
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::cobalt::LoggerFactory::Name_},
             .source = kMockCobaltMoniker,
             .targets = {kMemoryPressureProvider}},
            {.capability = Protocol{fuchsia::sysmem::Allocator::Name_},
             .source = AboveRoot(),
             .targets = {kMemoryPressureProvider, kWebViewMoniker}},
            {.capability = Protocol{fuchsia::scheduler::ProfileProvider::Name_},
             .source = AboveRoot(),
             .targets = {kMemoryPressureProvider}},
            {.capability = Protocol{fuchsia::kernel::RootJobForInspect::Name_},
             .source = AboveRoot(),
             .targets = {kMemoryPressureProvider}},
            {.capability = Protocol{fuchsia::kernel::Stats::Name_},
             .source = AboveRoot(),
             .targets = {kMemoryPressureProvider}},
            {.capability = Protocol{fuchsia::tracing::provider::Registry::Name_},
             .source = AboveRoot(),
             .targets = {kFontsProvider, kMemoryPressureProvider}},
            {.capability = Protocol{fuchsia::ui::scenic::Scenic::Name_},
             .source = kScenicMoniker,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::posix::socket::Provider::Name_},
             .source = kNetstack,
             .targets = {kWebViewMoniker}},
            {.capability = Protocol{fuchsia::ui::app::ViewProvider::Name_},
             .source = kWebViewMoniker,
             .targets = {AboveRoot()}}};
  }

  void SetUp() override {
    web_view_proxy_ = std::make_unique<WebViewProxy>(dispatcher());
    SemanticsIntegrationTestV2::SetUp();

    view_manager()->SetSemanticsEnabled(true);
    LaunchClient("web view");
  }

  bool NodeExistsWithLabel(std::string label) {
    auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
    if (!root) {
      return false;
    }

    auto node = FindNodeWithLabel(root, view_ref_koid(), label);
    return node != nullptr;
  }

  void RunLoopUntilNodeExistsWithLabel(std::string label) {
    RunLoopUntil([this, label] { return NodeExistsWithLabel(label); });
  }

  void LoadHtml(const std::string html) {
    web_view_proxy()->LoadHtml(html);
    RunLoopUntil([this] {
      auto node = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
      return node != nullptr;
    });
  }

 protected:
  WebViewProxy* web_view_proxy() { return web_view_proxy_.get(); }

 private:
  std::unique_ptr<WebViewProxy> web_view_proxy_;
};

TEST_F(WebSemanticsTest, StaticSemantics) {
  LoadHtml(kStaticHtml);

  RunLoopUntilNodeExistsWithLabel("Say something. Anything.");

  RunLoopUntilNodeExistsWithLabel("Click here");
}

TEST_F(WebSemanticsTest, PerformAction) {
  LoadHtml(kDynamicHtml);

  // Find the node with the counter to make sure it still reads 0
  RunLoopUntilNodeExistsWithLabel("0");
  // There shouldn't be a node labeled 1 yet
  EXPECT_FALSE(NodeExistsWithLabel("1"));

  // Trigger the button's default action
  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Increment");
  ASSERT_TRUE(node);
  EXPECT_TRUE(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::BUTTON);
  bool callback_handled = PerformAccessibilityAction(
      view_ref_koid(), node->node_id(), fuchsia::accessibility::semantics::Action::DEFAULT);
  EXPECT_TRUE(callback_handled);

  RunLoopUntilNodeExistsWithLabel("1");
}

// BUG(fxb.dev/60002): Disable this test until the flakes are resolved.
TEST_F(WebSemanticsTest, DISABLED_HitTesting) {
  LoadHtml(kStaticHtml);

  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // When performing hit tests, aim for just inside the node's bounding box.  Note
  // that for nodes from Chrome, the min corner has a larger y value than the max.
  fuchsia::math::PointF offset = {1., -1.};

  // Hit test the plain text
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Test 1 2 3... ");
  ASSERT_TRUE(node);
  auto hit_node = HitTest(
      view_ref_koid(), CalculateCenterOfSemanticNodeBoundingBoxCoordinate(view_ref_koid(), node));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());

  // Hit test the button
  node = FindNodeWithLabel(root, view_ref_koid(), "Click here");
  ASSERT_TRUE(node);
  hit_node = HitTest(view_ref_koid(),
                     CalculateCenterOfSemanticNodeBoundingBoxCoordinate(view_ref_koid(), node));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());
}

// BUG(fxb.dev/60002): Disable this test until the flakes are resolved.
TEST_F(WebSemanticsTest, DISABLED_ScrollToMakeVisible) {
  LoadHtml(kBigListHtml);

  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // The "Entry 999" node should be off-screen
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Entry 999");
  ASSERT_TRUE(node);

  // Record the location of a corner of the node's bounding box.  We record this rather than the
  // transform or the location fields since the runtime could change either when an element is
  // moved.
  auto node_corner =
      GetTransformForNode(view_ref_koid(), node->node_id()).Apply(node->location().min);

  bool callback_handled = PerformAccessibilityAction(
      view_ref_koid(), node->node_id(), fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN);
  EXPECT_TRUE(callback_handled);

  // Verify the "Entry 999" node has moved.  Note that this does not verify that it's now on screen,
  // since the semantics API does not encode enough information to be able to answer that
  // definitively.
  // TODO(fxb.dev/58276): Once we have the Semantic Event Updates work done, this logic can be
  // more clearly written as waiting for notification of an update then checking the tree.
  RunLoopUntil([this, root, &node_corner] {
    auto node = FindNodeWithLabel(root, view_ref_koid(), "Entry 999");
    if (node == nullptr) {
      return false;
    }

    auto new_node_corner =
        GetTransformForNode(view_ref_koid(), node->node_id()).Apply(node->location().min);
    return node_corner.x != new_node_corner.x || node_corner.y != new_node_corner.y ||
           node_corner.z != new_node_corner.z;
  });
}

}  // namespace
}  // namespace accessibility_test
