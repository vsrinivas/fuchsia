// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
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
#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

namespace accessibility_test {
namespace {

using component_testing::ChildRef;
using component_testing::Directory;
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

static constexpr auto kScrollingHtml = R"(
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

class WebSemanticsTest : public SemanticsIntegrationTestV2 {
 public:
  static constexpr auto kWebView = "web_view";
  static constexpr auto kWebViewRef = ChildRef{kWebView};
  static constexpr auto kWebViewUrl = "#meta/semantics-test-web-client.cm";

  static constexpr auto kFontsProvider = "fonts_provider";
  static constexpr auto kFontsProviderUrl = "#meta/fonts.cm";

  static constexpr auto kTextManager = "text_manager";
  static constexpr auto kTextManagerUrl = "#meta/text_manager.cm";

  static constexpr auto kIntl = "intl";
  static constexpr auto kIntlUrl = "#meta/intl_property_manager.cm";

  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderUrl = "#meta/memory_monitor.cm";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackUrl = "#meta/netstack.cm";

  static constexpr auto kWebContextProvider = "web_context_provider";
  static constexpr auto kWebContextProviderUrl =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cm";

  static constexpr auto kBuildInfoProvider = "build_info_provider";
  static constexpr auto kBuildInfoProviderUrl = "#meta/fake_build_info.cm";

  static constexpr auto kMockCobalt = "cobalt";
  static constexpr auto kMockCobaltUrl = "#meta/mock_cobalt.cm";

  WebSemanticsTest() = default;
  ~WebSemanticsTest() override = default;

  void ConfigureRealm() override {
    // First, add all child components of this test suite.
    realm()->AddChild(kWebView, kWebViewUrl);
    realm()->AddChild(kFontsProvider, kFontsProviderUrl);
    realm()->AddChild(kTextManager, kTextManagerUrl);
    realm()->AddChild(kIntl, kIntlUrl);
    realm()->AddChild(kMemoryPressureProvider, kMemoryPressureProviderUrl);
    realm()->AddChild(kNetstack, kNetstackUrl);
    realm()->AddChild(kWebContextProvider, kWebContextProviderUrl);
    realm()->AddChild(kBuildInfoProvider, kBuildInfoProviderUrl);
    realm()->AddChild(kMockCobalt, kMockCobaltUrl);

    // Second, add all necessary routing.
    realm()->AddRoute(
        {.capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
         .source = kSemanticsManagerRef,
         .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::fonts::Provider::Name_}},
                       .source = ChildRef{kFontsProvider},
                       .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_},
                                        Protocol{fuchsia::logger::LogSink::Name_},
                                        Directory{.name = "config-data",
                                                  .rights = fuchsia::io::R_STAR_DIR,
                                                  .path = "/config/data"}},
                       .source = ParentRef(),
                       .targets = {ChildRef{kFontsProvider}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::ui::input::ImeService::Name_}},
                       .source = ChildRef{kTextManager},
                       .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
                       .source = ChildRef{kMemoryPressureProvider},
                       .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::net::interfaces::State::Name_},
                                        Protocol{fuchsia::netstack::Netstack::Name_}},
                       .source = ChildRef{kNetstack},
                       .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                                        Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                       .source = ParentRef(),
                       .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::web::ContextProvider::Name_}},
                       .source = ChildRef{kWebContextProvider},
                       .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                       .source = ParentRef(),
                       .targets = {ChildRef{kFontsProvider}, ChildRef{kWebContextProvider}}});
    realm()->AddRoute(
        {.capabilities = {Protocol{fuchsia::metrics::MetricEventLoggerFactory::Name_}},
         .source = ChildRef{kMockCobalt},
         .targets = {ChildRef{kMemoryPressureProvider}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
                       .source = ParentRef(),
                       .targets = {ChildRef{kMemoryPressureProvider}, ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::kernel::RootJobForInspect::Name_},
                                        Protocol{fuchsia::kernel::Stats::Name_},
                                        Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                                        Protocol{fuchsia::tracing::provider::Registry::Name_}},
                       .source = ParentRef(),
                       .targets = {ChildRef{kMemoryPressureProvider}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_}},
                       .source = ChildRef{kNetstack},
                       .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::buildinfo::Provider::Name_}},
                       .source = ChildRef{kBuildInfoProvider},
                       .targets = {ChildRef{kWebView}, ChildRef{kWebContextProvider}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::intl::PropertyProvider::Name_}},
                       .source = ChildRef{kIntl},
                       .targets = {ChildRef{kWebView}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kWebView},
                       .targets = {ParentRef()}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::sys::Environment::Name_}},
                       .source = ParentRef(),
                       .targets = {ChildRef{kWebContextProvider}, ChildRef{kWebView}}});

    FX_LOGS(INFO) << "Override html config";
    // Override "html" config value for web client.
    realm()->InitMutableConfigToEmpty(kWebView);
    realm()->SetConfigValue(kWebView, "html", HtmlForTestCase());

    FX_LOGS(INFO) << "OVerrode html config";
  }

  void SetUp() override {
    SemanticsIntegrationTestV2::SetUp();

    SetupScene();

    view_manager()->SetSemanticsEnabled(true);

    FX_LOGS(INFO) << "Wait for root node";
    RunLoopUntil([this] {
      auto node = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
      return node != nullptr;
    });
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

 protected:
  // Returns the html to use for this test case.
  virtual std::string HtmlForTestCase() = 0;
};

class StaticHtmlTest : public WebSemanticsTest {
 public:
  StaticHtmlTest() = default;
  ~StaticHtmlTest() override = default;

  std::string HtmlForTestCase() override { return kStaticHtml; }
};

INSTANTIATE_TEST_SUITE_P(StaticHtmlTestWithParams, StaticHtmlTest,
                         ::testing::ValuesIn(SemanticsIntegrationTestV2::UIConfigurationsToTest()));
TEST_P(StaticHtmlTest, StaticSemantics) {
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
  RunLoopUntilNodeExistsWithLabel("Title");

  RunLoopUntilNodeExistsWithLabel("Paragraph");
}

TEST_P(StaticHtmlTest, HitTesting) {
  FX_LOGS(INFO) << "Wait for scale factor";
  WaitForScaleFactor();
  FX_LOGS(INFO) << "Received scale factor";

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

class DynamicHtmlTest : public WebSemanticsTest {
 public:
  DynamicHtmlTest() = default;
  ~DynamicHtmlTest() override = default;

  std::string HtmlForTestCase() override { return kDynamicHtml; }
};

INSTANTIATE_TEST_SUITE_P(DynamicHtmlTestWithParams, DynamicHtmlTest,
                         ::testing::ValuesIn(SemanticsIntegrationTestV2::UIConfigurationsToTest()));

TEST_P(DynamicHtmlTest, PerformAction) {
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

class ScrollingHtmlTest : public WebSemanticsTest {
 public:
  ScrollingHtmlTest() = default;
  ~ScrollingHtmlTest() override = default;

  std::string HtmlForTestCase() override { return kScrollingHtml; }
};

INSTANTIATE_TEST_SUITE_P(ScrollingHtmlTestWithParams, ScrollingHtmlTest,
                         ::testing::ValuesIn(SemanticsIntegrationTestV2::UIConfigurationsToTest()));

TEST_P(ScrollingHtmlTest, ScrollToMakeVisible) {
  FX_LOGS(INFO) << "Wait for scale factor";
  WaitForScaleFactor();
  FX_LOGS(INFO) << "Received scale factor";

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
