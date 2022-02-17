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
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
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

using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Route;

static constexpr auto kStaticHtml = R"(
<html>
  <head>
    <title>Title</title>
  </head>
  <body>
    <p>Paragraph</p>
    <p hidden>Hidden</p>
    <button type="button" aria-label="Button"></button>
  </body>
</html>
)";

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

static constexpr auto kOffscreenNodeHtml = R"(
<html>
  <head><title>accessibility 1</title></head>
  <body>
    <button>a button</button>
    <p>paragraph 1</p>
    <p>paragraph the second</p>
    <p>a third paragraph</p>
    <button>another button</button>
    <button>button 3</button>
    <input type="range" min="0" max="100" value="51" step="3" class="slider" id="myRange">
    <div style='height:2000px; width:2000px;'></div>
    <p>offscreen node</p>
    <button>button 4</button>
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
class WebViewProxy : public fuchsia::ui::app::ViewProvider, public LocalComponent {
 public:
  explicit WebViewProxy(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher),
        navigation_event_listener_(),
        navigation_event_listener_binding_(&navigation_event_listener_) {}

  ~WebViewProxy() override = default;

  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override {
    FX_LOGS(INFO) << "Starting WebViewProxy";
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

    // Set up navigation affordances.
    web_frame_->SetNavigationEventListener(navigation_event_listener_binding_.NewBinding());
    web_frame_->GetNavigationController(navigation_controller_.NewRequest());

    // Publish the ViewProvider service.
    FX_CHECK(
        mock_handles->outgoing()->AddPublicService(
            fidl::InterfaceRequestHandler<fuchsia::ui::app::ViewProvider>([this](auto request) {
              bindings_.AddBinding(this, std::move(request), dispatcher_);
            })) == ZX_OK);
    mock_handles_.emplace_back(std::move(mock_handles));
  }

  void LoadHtml(std::string html) {
    // Load the web page.
    FX_LOGS(INFO) << "Loading web page";
    navigation_controller_->LoadUrl("about:blank", fuchsia::web::LoadUrlParams(), [](auto result) {
      if (result.is_err()) {
        FX_LOGS(FATAL) << "Error while loading URL: " << static_cast<uint32_t>(result.err());
      } else {
        FX_LOGS(INFO) << "Loaded about:blank";
      }
    });

    FX_LOGS(INFO) << "Running javascript to inject html";
    web_frame_->ExecuteJavaScript(
        {"*"}, BufferFromString(fxl::StringPrintf("document.write(`%s`);", html.c_str())),
        [](auto result) {
          if (result.is_err()) {
            FX_LOGS(FATAL) << "Error while executing JavaScript: "
                           << static_cast<uint32_t>(result.err());
          } else {
            FX_LOGS(INFO) << "Injected html";
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
  std::vector<std::unique_ptr<LocalComponentHandles>> mock_handles_{};
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;
  NavListener navigation_event_listener_;
  fidl::Binding<fuchsia::web::NavigationEventListener> navigation_event_listener_binding_;
  fuchsia::web::NavigationControllerPtr navigation_controller_;
  fuchsia::ui::views::ViewToken view_token_;
  fuchsia::web::ContextPtr web_context_;
  fuchsia::web::FramePtr web_frame_;
};

class WebSemanticsTest : public SemanticsIntegrationTestV2 {
 public:
  static constexpr auto kWebView = "web_view";
  static constexpr auto kWebViewRef = ChildRef{kWebView};

  static constexpr auto kFontsProvider = "fonts_provider";
  static constexpr auto kFontsProviderRef = ChildRef{kFontsProvider};
  static constexpr auto kFontsProviderUrl = "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx";

  static constexpr auto kTextManager = "text_manager";
  static constexpr auto kTextManagerRef = ChildRef{kTextManager};
  static constexpr auto kTextManagerUrl =
      "fuchsia-pkg://fuchsia.com/text_manager#meta/text_manager.cmx";

  static constexpr auto kIntl = "intl";
  static constexpr auto kIntlRef = ChildRef{kIntl};
  static constexpr auto kIntlUrl =
      "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager.cmx";

  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderRef = ChildRef{kMemoryPressureProvider};
  static constexpr auto kMemoryPressureProviderUrl =
      "fuchsia-pkg://fuchsia.com/memory_monitor#meta/memory_monitor.cmx";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackRef = ChildRef{kNetstack};
  static constexpr auto kNetstackUrl =
      "fuchsia-pkg://fuchsia.com/semantics-integration-tests#meta/netstack.cmx";

  static constexpr auto kWebContextProvider = "web_context_provider";
  static constexpr auto kWebContextProviderRef = ChildRef{kWebContextProvider};
  static constexpr auto kWebContextProviderUrl =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx";

  WebSemanticsTest() = default;
  ~WebSemanticsTest() override = default;

  WebViewProxy* web_view_proxy() const { return web_view_proxy_.get(); }

  void ConfigureRealm(RealmBuilder* realm_builder) override {
    // First, add all child components of this test suite.
    realm_builder->AddLocalChild(kWebView, web_view_proxy());
    realm_builder->AddLegacyChild(kFontsProvider, kFontsProviderUrl);
    realm_builder->AddLegacyChild(kTextManager, kTextManagerUrl);
    realm_builder->AddLegacyChild(kIntl, kIntlUrl);
    realm_builder->AddLegacyChild(kMemoryPressureProvider, kMemoryPressureProviderUrl);
    realm_builder->AddLegacyChild(kNetstack, kNetstackUrl);
    realm_builder->AddLegacyChild(kWebContextProvider, kWebContextProviderUrl);

    // Second, add all necessary routing.
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::fonts::Provider::Name_}},
                             .source = kFontsProviderRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::ui::input::ImeService::Name_}},
                             .source = kTextManagerRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute(
        {.capabilities = {Protocol{fuchsia::ui::input::ImeVisibilityService::Name_}},
         .source = kTextManagerRef,
         .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::intl::PropertyProvider::Name_}},
                             .source = kIntlRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
                             .source = kMemoryPressureProviderRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::netstack::Netstack::Name_}},
                             .source = kNetstackRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::net::interfaces::State::Name_}},
                             .source = kNetstackRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute(
        {.capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
         .source = kSemanticsManagerRef,
         .targets = {kWebViewRef, kWebContextProviderRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::web::ContextProvider::Name_}},
                             .source = kWebContextProviderRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::sys::Environment::Name_}},
                             .source = ParentRef(),
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::cobalt::LoggerFactory::Name_}},
                             .source = kMockCobaltRef,
                             .targets = {kMemoryPressureProviderRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
                             .source = ParentRef(),
                             .targets = {kMemoryPressureProviderRef, kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::scheduler::ProfileProvider::Name_}},
                             .source = ParentRef(),
                             .targets = {kMemoryPressureProviderRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::kernel::RootJobForInspect::Name_}},
                             .source = ParentRef(),
                             .targets = {kMemoryPressureProviderRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::kernel::Stats::Name_}},
                             .source = ParentRef(),
                             .targets = {kMemoryPressureProviderRef}});
    realm_builder->AddRoute(
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
         .source = ParentRef(),
         .targets = {kFontsProviderRef, kMemoryPressureProviderRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                             .source = kScenicRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_}},
                             .source = kNetstackRef,
                             .targets = {kWebViewRef}});
    realm_builder->AddRoute({.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                             .source = kWebViewRef,
                             .targets = {ParentRef()}});
  }

  void SetUp() override {
    web_view_proxy_ = std::make_unique<WebViewProxy>(dispatcher());
    SemanticsIntegrationTestV2::SetUp();

    FX_LOGS(INFO) << "Setting semantics enabled";
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
    FX_LOGS(INFO) << "Waiting until node exists with label: " << label
                  << " in tree with koid: " << view_ref_koid();
    RunLoopUntil([this, label] { return NodeExistsWithLabel(label); });
    FX_LOGS(INFO) << "Found node with label: " << label
                  << " in tree with koid: " << view_ref_koid();
  }

  void LoadHtml(const std::string html) {
    FX_LOGS(INFO) << "Loading html: " << html;
    web_view_proxy()->LoadHtml(html);
    RunLoopUntil([this] {
      auto node = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
      return node != nullptr;
    });
    FX_LOGS(INFO) << "Finished loading html";
  }

 protected:
  WebViewProxy* web_view_proxy() { return web_view_proxy_.get(); }

 private:
  std::unique_ptr<WebViewProxy> web_view_proxy_;
};

TEST_F(WebSemanticsTest, StaticSemantics) {
  /* The semantic tree for static.html:
   *
   * ID: 0 Label:Title Role: UNKNOWN
   *     ID: 2 Label:no label Role: UNKNOWN
   *         ID: 3 Label:no label Role: UNKNOWN
   *             ID: 4 Label:no label Role: UNKNOWN
   *                 ID: 6 Label:Paragraph Role: STATIC_TEXT
   *                     ID: 8 Label:Paragraph Role: UNKNOWN
   *             ID: 5 Label:Button Role: BUTTON
   */
  LoadHtml(kStaticHtml);

  RunLoopUntilNodeExistsWithLabel("Title");

  RunLoopUntilNodeExistsWithLabel("Paragraph");
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

TEST_F(WebSemanticsTest, HitTesting) {
  LoadHtml(kStaticHtml);

  // Ensure that chrome has received the device scale from scenic, and updated
  // the fuchsia root node's transform to reflect the value. This is necessary
  // to avoid a race condition in which chrome has received the device scale,
  // but fuchsia has not, which could result in a false hit test miss.
  RunLoopUntil([this] {
    auto node = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
    // TODO(fxb.dev/93943): Remove accommodation for transform field.
    return (node->has_transform() && node->transform().matrix[0] != 1.f) ||
           (node->has_node_to_container_transform() &&
            node->node_to_container_transform().matrix[0] != 1.f);
  });

  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // Hit test the plain text
  RunLoopUntilNodeExistsWithLabel("Paragraph");
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Paragraph");
  ASSERT_TRUE(node);
  auto hit_node = HitTest(
      view_ref_koid(), CalculateCenterOfSemanticNodeBoundingBoxCoordinate(view_ref_koid(), node));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());

  // Hit test the button
  RunLoopUntilNodeExistsWithLabel("Button");
  node = FindNodeWithLabel(root, view_ref_koid(), "Button");
  ASSERT_TRUE(node);
  hit_node = HitTest(view_ref_koid(),
                     CalculateCenterOfSemanticNodeBoundingBoxCoordinate(view_ref_koid(), node));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());
}

TEST_F(WebSemanticsTest, ScrollToMakeVisible) {
  LoadHtml(kOffscreenNodeHtml);

  RunLoopUntil([this] {
    auto node = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
    // TODO(fxb.dev/93943): Remove accommodation for transform field.
    return (node->has_transform() && node->transform().matrix[0] != 1.f) ||
           (node->has_node_to_container_transform() &&
            node->node_to_container_transform().matrix[0] != 1.f);
  });

  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // The offscreen node should be off-screen.
  RunLoopUntilNodeExistsWithLabel("offscreen node");
  auto node = FindNodeWithLabel(root, view_ref_koid(), "offscreen node");
  ASSERT_TRUE(node);

  bool callback_handled = PerformAccessibilityAction(
      view_ref_koid(), node->node_id(), fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN);
  EXPECT_TRUE(callback_handled);

  // Verify that the root container was scrolled to make the offscreen node
  // visible.
  // TODO(fxb.dev/58276): Once we have the Semantic Event Updates work done, this logic can be
  // more clearly written as waiting for notification of an update then checking the tree.
  RunLoopUntil([this /*&node_corner*/] {
    auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
    return root->has_states() && root->states().has_viewport_offset() &&
           root->states().viewport_offset().y != 0;
  });
}

}  // namespace
}  // namespace accessibility_test
