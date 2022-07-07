// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tests/integration_flutter_tests/embedder/flutter-embedder-test-ip.h"

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

namespace {

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::DirectoryContents;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::RealmRoot;
using component_testing::Route;
using component_testing::StartupMode;

static constexpr auto kChildFlutterRealm = "child_flutter";
static constexpr auto kChildFlutterRealmRef = ChildRef{kChildFlutterRealm};
static constexpr auto kParentFlutterRealm = "parent_flutter";
static constexpr auto kParentFlutterRealmRef = ChildRef{kParentFlutterRealm};
static constexpr auto kInputPipelineTestRealm = "input_pipeline_test_realm";
static constexpr auto kInputPipelineTestRealmRef = ChildRef{kInputPipelineTestRealm};
static constexpr auto kInputPipelineTestRealmUrl = "#meta/root_presenter_scene_with_input.cm";

}  // namespace

namespace flutter_embedder_test_ip {

constexpr char kChildViewUrl[] = "fuchsia-pkg://fuchsia.com/child-view#meta/child-view-realm.cm";
constexpr char kParentViewUrl[] = "fuchsia-pkg://fuchsia.com/parent-view#meta/parent-view-realm.cm";

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

void FlutterEmbedderTestIp::SetUpRealmBase() {
  FX_LOGS(INFO) << "Setting up realm base.";

  // Add base components.
  realm_builder_.AddChild(kInputPipelineTestRealm, kInputPipelineTestRealmUrl);

  // Add embedded child component to realm.
  realm_builder_.AddChild(kChildFlutterRealm, kChildViewUrl);

  // Add child flutter app routes. Note that we do not route ViewProvider to ParentRef{} as it is
  // embedded.
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                .source = kInputPipelineTestRealmRef,
                                .targets = {kChildFlutterRealmRef}});
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::sys::Environment::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_}},
            .source = ParentRef{},
            .targets = {kChildFlutterRealmRef}});

  // Add base routes.
  realm_builder_.AddRoute(Route{.capabilities =
                                    {
                                        Protocol{fuchsia::logger::LogSink::Name_},
                                        Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                                        Protocol{fuchsia::sysmem::Allocator::Name_},
                                        Protocol{fuchsia::tracing::provider::Registry::Name_},
                                        Protocol{fuchsia::vulkan::loader::Loader::Name_},
                                    },
                                .source = ParentRef{},
                                .targets = {kInputPipelineTestRealmRef}});

  // Capabilities routed to test driver.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::input::injection::InputDeviceRegistry::Name_},
                             Protocol{fuchsia::ui::pointerinjector::Registry::Name_},
                             Protocol{fuchsia::ui::policy::Presenter::Name_},
                             Protocol{fuchsia::ui::scenic::Scenic::Name_}},
            .source = kInputPipelineTestRealmRef,
            .targets = {ParentRef{}}});
}

void FlutterEmbedderTestIp::BuildRealmAndLaunchApp(const std::string& component_url,
                                                   const std::vector<std::string>& component_args) {
  FX_LOGS(INFO) << "Building realm with component: " << component_url;
  realm_builder_.AddChild(kParentFlutterRealm, kParentViewUrl);

  // Capabilities routed to embedded flutter app.
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                .source = kInputPipelineTestRealmRef,
                                .targets = {kParentFlutterRealmRef}});
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::sys::Environment::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_}},
            .source = ParentRef{},
            .targets = {kParentFlutterRealmRef}});

  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                                .source = kParentFlutterRealmRef,
                                .targets = {ParentRef()}});

  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                                .source = kChildFlutterRealmRef,
                                .targets = {kParentFlutterRealmRef}});

  if (!component_args.empty()) {
    // Construct a args.csv file containing the specified comma-separated component args.
    std::string csv;
    for (const auto& arg : component_args) {
      csv += arg + ',';
    }
    // Remove last comma.
    csv.pop_back();

    auto config_directory_contents = DirectoryContents();
    config_directory_contents.AddFile("args.csv", csv);
    realm_builder_.RouteReadOnlyDirectory("config-data", {kParentFlutterRealmRef},
                                          std::move(config_directory_contents));
  }
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

TEST_F(FlutterEmbedderTestIp, Embedding) {
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

TEST_F(FlutterEmbedderTestIp, HittestEmbedding) {
  BuildRealmAndLaunchApp(kParentViewUrl);

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Tap the center of child view.
  RegisterInjectionDevice();
  TryInject();

  // Take screenshot until we see the child-view's tapped color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildTappedColor, [](std::map<scenic::Color, size_t> histogram) {
    // Expect parent and child background colors, with parent color > child color.
    EXPECT_GT(histogram[kParentBackgroundColor], 0u);
    EXPECT_EQ(histogram[kChildBackgroundColor], 0u);
    EXPECT_GT(histogram[kChildTappedColor], 0u);
    EXPECT_GT(histogram[kParentBackgroundColor], histogram[kChildTappedColor]);
  }));
}

TEST_F(FlutterEmbedderTestIp, HittestDisabledEmbedding) {
  BuildRealmAndLaunchApp(kParentViewUrl, {"--no-hitTestable"});

  // Take screenshots until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Tap the center of child view. Since it's not hit-testable, the tap should go to the parent.
  RegisterInjectionDevice();
  TryInject();

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

TEST_F(FlutterEmbedderTestIp, EmbeddingWithOverlay) {
  BuildRealmAndLaunchApp(kParentViewUrl, {"--showOverlay"});

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

TEST_F(FlutterEmbedderTestIp, HittestEmbeddingWithOverlay) {
  BuildRealmAndLaunchApp(kParentViewUrl, {"--showOverlay"});

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Tap the center of child view.
  RegisterInjectionDevice();

  // The bottom-left corner of the overlay is at the center of the screen,
  // which is at (0, 0) in the injection coordinate space. The local coordinates
  // of an injected tap event are subject to rounding error, so injecting at
  // exactly (0, 0) may cause unexpected behavior. Instead, we inject slightly
  // below and left of the center.
  //
  // TODO(fxbug.dev/103612): Inject at (0, 0).
  TryInject(/* x = */ -1, /* y = */ 1);

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

}  // namespace flutter_embedder_test_ip
