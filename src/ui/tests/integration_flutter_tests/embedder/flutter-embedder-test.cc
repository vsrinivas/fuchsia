// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tests/integration_flutter_tests/embedder/flutter-embedder-test.h"

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

namespace {

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::RealmRoot;
using component_testing::Route;
using RealmBuilder = component_testing::RealmBuilder;

constexpr auto kRootPresenter = "root_presenter";
constexpr auto kRootPresenterRef = ChildRef{kRootPresenter};
constexpr auto kScenicTestRealm = "scenic_test_realm";
constexpr auto kScenicTestRealmRef = ChildRef{kScenicTestRealm};
constexpr auto kHdcp = "hdcp";
constexpr auto kHdcpRef = ChildRef{kHdcp};
constexpr auto kParentFlutter = "parent_flutter";
constexpr auto kParentFlutterRef = ChildRef{kParentFlutter};

}  // namespace

namespace flutter_embedder_test {

constexpr char kParentViewUrl[] = "fuchsia-pkg://fuchsia.com/parent-view#meta/parent-view.cmx";
constexpr char kParentViewDisabledHittestUrl[] =
    "fuchsia-pkg://fuchsia.com/parent-view#meta/parent-view-disabled-hittest.cmx";
constexpr char kParentViewShowOverlayUrl[] =
    "fuchsia-pkg://fuchsia.com/parent-view#meta/parent-view-show-overlay.cmx";

constexpr scenic::Color kParentBackgroundColor = {0x00, 0x00, 0xFF, 0xFF};  // Blue
constexpr scenic::Color kParentTappedColor = {0x00, 0x00, 0x00, 0xFF};      // Black
constexpr scenic::Color kChildBackgroundColor = {0xFF, 0x00, 0xFF, 0xFF};   // Pink
constexpr scenic::Color kChildTappedColor = {0xFF, 0xFF, 0x00, 0xFF};       // Yellow

// TODO(fxb/64201): Remove forced opacity colors when Flatland is enabled.
constexpr scenic::Color kOverlayBackgroundColor1 = {0x00, 0xFF, 0x0E,
                                                    0xFF};  // Green, blended with blue (FEMU local)
constexpr scenic::Color kOverlayBackgroundColor2 = {0x0E, 0xFF, 0x0E,
                                                    0xFF};  // Green, blended with pink (FEMU local)
constexpr scenic::Color kOverlayBackgroundColor3 = {0x00, 0xFF, 0x0D,
                                                    0xFF};  // Green, blended with blue (AEMU infra)
constexpr scenic::Color kOverlayBackgroundColor4 = {0x0D, 0xFF, 0x0D,
                                                    0xFF};  // Green, blended with pink (AEMU infra)
constexpr scenic::Color kOverlayBackgroundColor5 = {0x00, 0xFE, 0x0D,
                                                    0xFF};  // Green, blended with blue (NUC)
constexpr scenic::Color kOverlayBackgroundColor6 = {0x0D, 0xFF, 0x00,
                                                    0xFF};  // Green, blended with pink (NUC)

static size_t OverlayPixelCount(std::map<scenic::Color, size_t>& histogram) {
  return histogram[kOverlayBackgroundColor1] + histogram[kOverlayBackgroundColor2] +
         histogram[kOverlayBackgroundColor3] + histogram[kOverlayBackgroundColor4] +
         histogram[kOverlayBackgroundColor5] + histogram[kOverlayBackgroundColor6];
}

void FlutterEmbedderTest::SetUpRealmBase() {
  FX_LOGS(INFO) << "Setting up realm base.";
  // Add base components.
  realm_builder_.AddChild(kRootPresenter,
                          "fuchsia-pkg://fuchsia.com/flutter-embedder-test#meta/root_presenter.cm");
  realm_builder_.AddChild(
      kScenicTestRealm,
      "fuchsia-pkg://fuchsia.com/flutter-embedder-test#meta/scenic-test-realm.cm");
  realm_builder_.AddLegacyChild(
      kHdcp, "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx");

  // Add base routes.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_}},
            .source = ParentRef{},
            .targets = {kScenicTestRealmRef}});
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
                                .source = ParentRef{},
                                .targets = {kScenicTestRealmRef, kHdcpRef}});
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
            .source = ParentRef{},
            .targets = {kScenicTestRealmRef, kRootPresenterRef, kHdcpRef}});

  // Route between siblings.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::hardware::display::Provider::Name_}},
            .source = kHdcpRef,
            .targets = {kScenicTestRealmRef}});

  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_},
                             Protocol{fuchsia::ui::pointerinjector::Registry::Name_}},
            .source = kScenicTestRealmRef,
            .targets = {kRootPresenterRef}});

  // Capabilities routed to test driver.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::policy::Presenter::Name_},
                             Protocol{fuchsia::ui::input::InputDeviceRegistry::Name_}},
            .source = kRootPresenterRef,
            .targets = {ParentRef{}}});
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                .source = kScenicTestRealmRef,
                                .targets = {ParentRef{}}});
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::hardware::display::Provider::Name_}},
            .source = kHdcpRef,
            .targets = {ParentRef{}}});
}

void FlutterEmbedderTest::BuildRealmAndLaunchApp(const std::string& component_url) {
  FX_LOGS(INFO) << "Building realm with component: " << component_url;
  realm_builder_.AddLegacyChild(kParentFlutter, component_url);

  // Capabilities routed to embedded flutter app.
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                .source = ChildRef{kScenicTestRealm},
                                .targets = {kParentFlutterRef}});
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::sys::Environment::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_}},
            .source = ParentRef{},
            .targets = {kParentFlutterRef}});

  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                                .source = kParentFlutterRef,
                                .targets = {ParentRef()}});
  realm_ = std::make_unique<RealmRoot>(realm_builder_.Build());

  scenic_ = realm_->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([](zx_status_t status) {
    FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
  });

  FX_LOGS(INFO) << "Launching component: " << component_url;
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
  auto view_provider = realm_->Connect<fuchsia::ui::app::ViewProvider>();
  view_provider->CreateViewWithViewRef(std::move(view_token.value), std::move(view_ref_control),
                                       std::move(view_ref));

  // Present the view.
  embedder_view_.emplace(CreatePresentationContext(), std::move(view_holder_token));

  // Embed the view.
  std::optional<bool> view_state_changed_observed;
  embedder_view_->EmbedView(
      [&view_state_changed_observed](auto) { view_state_changed_observed = true; });

  RunLoopUntil([&view_state_changed_observed] { return view_state_changed_observed.has_value(); });
  ASSERT_TRUE(view_state_changed_observed.value());
  FX_LOGS(INFO) << "Launched component: " << component_url;
}

TEST_F(FlutterEmbedderTest, Embedding) {
  BuildRealmAndLaunchApp(kParentViewUrl);

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(
      TakeScreenshotUntil(kChildBackgroundColor, [](std::map<scenic::Color, size_t> histogram) {
        // Expect parent and child background colors, with parent color > child color.
        EXPECT_GT(histogram[kParentBackgroundColor], 0u);
        EXPECT_GT(histogram[kChildBackgroundColor], 0u);
        EXPECT_GT(histogram[kParentBackgroundColor], histogram[kChildBackgroundColor]);
      }));
}

TEST_F(FlutterEmbedderTest, HittestEmbedding) {
  BuildRealmAndLaunchApp(kParentViewUrl);

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Tap the center of child view.
  InjectInput();

  // Take screenshot until we see the child-view's tapped color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildTappedColor, [](std::map<scenic::Color, size_t> histogram) {
    // Expect parent and child background colors, with parent color > child color.
    EXPECT_GT(histogram[kParentBackgroundColor], 0u);
    EXPECT_EQ(histogram[kChildBackgroundColor], 0u);
    EXPECT_GT(histogram[kChildTappedColor], 0u);
    EXPECT_GT(histogram[kParentBackgroundColor], histogram[kChildTappedColor]);
  }));
}

TEST_F(FlutterEmbedderTest, HittestDisabledEmbedding) {
  BuildRealmAndLaunchApp(kParentViewDisabledHittestUrl);

  // Take screenshots until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Tap the center of child view. Since it's not hit-testable, the tap should go to the parent.
  InjectInput();

  // The parent-view should change color.
  ASSERT_TRUE(
      TakeScreenshotUntil(kParentTappedColor, [](std::map<scenic::Color, size_t> histogram) {
        // Expect parent and child background colors, with parent color > child color.
        EXPECT_EQ(histogram[kParentBackgroundColor], 0u);
        EXPECT_GT(histogram[kParentTappedColor], 0u);
        EXPECT_GT(histogram[kChildBackgroundColor], 0u);
        EXPECT_EQ(histogram[kChildTappedColor], 0u);
        EXPECT_GT(histogram[kParentTappedColor], histogram[kChildBackgroundColor]);
      }));
}

TEST_F(FlutterEmbedderTest, EmbeddingWithOverlay) {
  BuildRealmAndLaunchApp(kParentViewShowOverlayUrl);

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(
      TakeScreenshotUntil(kChildBackgroundColor, [](std::map<scenic::Color, size_t> histogram) {
        // Expect parent, overlay and child background colors.
        // With parent color > child color and overlay color > child color.
        const size_t overlay_pixel_count = OverlayPixelCount(histogram);
        EXPECT_GT(histogram[kParentBackgroundColor], 0u);
        EXPECT_GT(overlay_pixel_count, 0u);
        EXPECT_GT(histogram[kChildBackgroundColor], 0u);
        EXPECT_GT(histogram[kParentBackgroundColor], histogram[kChildBackgroundColor]);
        EXPECT_GT(overlay_pixel_count, histogram[kChildBackgroundColor]);
      }));
}

TEST_F(FlutterEmbedderTest, HittestEmbeddingWithOverlay) {
  BuildRealmAndLaunchApp(kParentViewShowOverlayUrl);

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Tap the center of child view.
  InjectInput();

  // Take screenshot until we see the child-view's tapped color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildTappedColor, [](std::map<scenic::Color, size_t> histogram) {
    // Expect parent, overlay and child background colors.
    // With parent color > child color and overlay color > child color.
    const size_t overlay_pixel_count = OverlayPixelCount(histogram);
    EXPECT_GT(histogram[kParentBackgroundColor], 0u);
    EXPECT_GT(overlay_pixel_count, 0u);
    EXPECT_EQ(histogram[kChildBackgroundColor], 0u);
    EXPECT_GT(histogram[kChildTappedColor], 0u);
    EXPECT_GT(histogram[kParentBackgroundColor], histogram[kChildTappedColor]);
    EXPECT_GT(overlay_pixel_count, histogram[kChildTappedColor]);
  }));
}

}  // namespace flutter_embedder_test
