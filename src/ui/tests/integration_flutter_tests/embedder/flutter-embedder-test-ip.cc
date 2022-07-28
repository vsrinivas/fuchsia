// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tests/integration_flutter_tests/embedder/flutter-embedder-test-ip.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
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
static constexpr auto kTestUIStack = "ui";
static constexpr auto kTestUIStackRef = ChildRef{kTestUIStack};
static constexpr auto kTestUIStackUrl =
    "fuchsia-pkg://fuchsia.com/test-ui-stack#meta/test-ui-stack.cm";

bool CheckViewExistsInSnapshot(const fuchsia::ui::observation::geometry::ViewTreeSnapshot& snapshot,
                               zx_koid_t view_ref_koid) {
  if (!snapshot.has_views()) {
    return false;
  }

  auto snapshot_count = std::count_if(
      snapshot.views().begin(), snapshot.views().end(),
      [view_ref_koid](const auto& view) { return view.view_ref_koid() == view_ref_koid; });

  return snapshot_count > 0;
}

bool CheckViewExistsInUpdates(
    const std::vector<fuchsia::ui::observation::geometry::ViewTreeSnapshot>& updates,
    zx_koid_t view_ref_koid) {
  auto update_count =
      std::count_if(updates.begin(), updates.end(), [view_ref_koid](auto& snapshot) {
        return CheckViewExistsInSnapshot(snapshot, view_ref_koid);
      });

  return update_count > 0;
}

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

  // Add test UI stack component.
  realm_builder_.AddChild(kTestUIStack, kTestUIStackUrl);

  // Add embedded child component to realm.
  realm_builder_.AddChild(kChildFlutterRealm, kChildViewUrl);

  // Add child flutter app routes. Note that we do not route ViewProvider to ParentRef{} as it is
  // embedded.
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                .source = kTestUIStackRef,
                                .targets = {kChildFlutterRealmRef}});

  // Route base system services to flutter and the test UI stack.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                             Protocol{fuchsia::sys::Environment::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_}},
            .source = ParentRef{},
            .targets = {kChildFlutterRealmRef, kTestUIStackRef}});

  // Capabilities routed to test driver.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::test::input::Registry::Name_},
                             Protocol{fuchsia::ui::test::scene::Provider::Name_},
                             Protocol{fuchsia::ui::scenic::Scenic::Name_}},
            .source = kTestUIStackRef,
            .targets = {ParentRef{}}});
}

// Checks whether the view with |view_ref_koid| has connected to the view tree. The response of a
// f.u.o.g.Provider.Watch call is stored in |watch_response| if it contains |view_ref_koid|.
bool FlutterEmbedderTestIp::HasViewConnected(
    const fuchsia::ui::observation::geometry::ProviderPtr& geometry_provider,
    std::optional<fuchsia::ui::observation::geometry::ProviderWatchResponse>& watch_response,
    zx_koid_t view_ref_koid) {
  std::optional<fuchsia::ui::observation::geometry::ProviderWatchResponse> geometry_result;
  geometry_provider->Watch(
      [&geometry_result](auto response) { geometry_result = std::move(response); });
  FX_LOGS(INFO) << "Waiting for geometry result";
  RunLoopUntil([&geometry_result] { return geometry_result.has_value(); });
  FX_LOGS(INFO) << "Received geometry result";
  if (CheckViewExistsInUpdates(geometry_result->updates(), view_ref_koid)) {
    watch_response = std::move(geometry_result);
  };
  return watch_response.has_value();
}

void FlutterEmbedderTestIp::BuildRealmAndLaunchApp(const std::string& component_url,
                                                   const std::vector<std::string>& component_args) {
  FX_LOGS(INFO) << "Building realm with component: " << component_url;
  realm_builder_.AddChild(kParentFlutterRealm, kParentViewUrl);

  // Capabilities routed to embedded flutter app.
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                .source = kTestUIStackRef,
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

  // Register fake touch screen device.
  RegisterTouchScreen();

  // Instruct Root Presenter to present test's View.
  std::optional<zx_koid_t> view_ref_koid;
  scene_provider_ = realm_->Connect<fuchsia::ui::test::scene::Provider>();
  scene_provider_.set_error_handler(
      [](auto) { FX_LOGS(ERROR) << "Error from test scene provider"; });
  fuchsia::ui::test::scene::ProviderAttachClientViewRequest request;
  request.set_view_provider(realm_->Connect<fuchsia::ui::app::ViewProvider>());
  scene_provider_->RegisterGeometryObserver(geometry_provider_.NewRequest(), []() {});
  scene_provider_->AttachClientView(
      std::move(request),
      [&view_ref_koid](auto client_view_ref_koid) { view_ref_koid = client_view_ref_koid; });

  FX_LOGS(INFO) << "Waiting for client view ref koid";
  RunLoopUntil([&view_ref_koid] { return view_ref_koid.has_value(); });

  // Wait for the client view to get attached to the view tree.
  std::optional<fuchsia::ui::observation::geometry::ProviderWatchResponse> watch_response;
  FX_LOGS(INFO) << "Waiting for client view to render";
  RunLoopUntil([this, &watch_response, &view_ref_koid] {
    return HasViewConnected(geometry_provider_, watch_response, *view_ref_koid);
  });
  FX_LOGS(INFO) << "Client view has rendered";

  scenic_ = realm_->Connect<fuchsia::ui::scenic::Scenic>();
  FX_LOGS(INFO) << "Launched component: " << component_url;
}

void FlutterEmbedderTestIp::RegisterTouchScreen() {
  FX_LOGS(INFO) << "Registering fake touch screen";
  input_registry_ = realm_->Connect<fuchsia::ui::test::input::Registry>();
  input_registry_.set_error_handler([](auto) { FX_LOGS(ERROR) << "Error from input helper"; });
  bool touchscreen_registered = false;
  fuchsia::ui::test::input::RegistryRegisterTouchScreenRequest request;
  request.set_device(fake_touchscreen_.NewRequest());
  input_registry_->RegisterTouchScreen(
      std::move(request), [&touchscreen_registered]() { touchscreen_registered = true; });
  RunLoopUntil([&touchscreen_registered] { return touchscreen_registered; });
  FX_LOGS(INFO) << "Touchscreen registered";
}

void FlutterEmbedderTestIp::InjectTap(int32_t x, int32_t y) {
  fuchsia::ui::test::input::TouchScreenSimulateTapRequest tap_request;
  tap_request.mutable_tap_location()->x = x;
  tap_request.mutable_tap_location()->y = y;
  fake_touchscreen_->SimulateTap(std::move(tap_request), [x, y]() {
    FX_LOGS(INFO) << "Tap injected at (" << x << ", " << y << ")";
  });
}

void FlutterEmbedderTestIp::TryInject(int32_t x, int32_t y) {
  InjectTap(x, y);
  async::PostDelayedTask(
      dispatcher(), [this, x, y] { TryInject(x, y); }, kTapRetryInterval);
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

  // Simulate a tap at the center of the child view.
  TryInject(/* x = */ 0, /* y = */ 0);

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

  // Simulate a tap at the center of the child view.
  TryInject(/* x = */ 0, /* y = */ 0);

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

  // The bottom-left corner of the overlay is at the center of the screen,
  // which is at (0, 0) in the injection coordinate space. Inject a pointer
  // event just outside the overlay's bounds, and ensure that it goes to the
  // embedded view.
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
