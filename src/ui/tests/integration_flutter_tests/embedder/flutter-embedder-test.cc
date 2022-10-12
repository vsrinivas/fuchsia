// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tests/integration_flutter_tests/embedder/flutter-embedder-test.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/display/singleton/cpp/fidl.h>
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
static constexpr auto kTestUIStack = "ui";
static constexpr auto kTestUIStackRef = ChildRef{kTestUIStack};
static constexpr auto kUsePointerInjection2Args = "--usePointerInjection2";

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

namespace flutter_embedder_test {

constexpr char kChildViewUrl[] = "fuchsia-pkg://fuchsia.com/child-view#meta/child-view-realm.cm";
constexpr char kParentViewUrl[] = "fuchsia-pkg://fuchsia.com/parent-view#meta/parent-view-realm.cm";

const ui_testing::Pixel kParentBackgroundColor(0xFF, 0x00, 0x00, 0xFF);  // Blue
const ui_testing::Pixel kParentTappedColor(0x00, 0x00, 0x00, 0xFF);      // Black
const ui_testing::Pixel kChildBackgroundColor(0xFF, 0x00, 0xFF, 0xFF);   // Pink
const ui_testing::Pixel kChildTappedColor(0x00, 0xFF, 0xFF, 0xFF);       // Yellow

// TODO(fxb/64201): Remove forced opacity colors when Flatland is enabled.
const ui_testing::Pixel kOverlayBackgroundColor1(0x0E, 0xFF, 0x00,
                                                 0xFF);  // Green, blended with blue (FEMU local)
const ui_testing::Pixel kOverlayBackgroundColor2(0x0E, 0xFF, 0x0E,
                                                 0xFF);  // Green, blended with pink (FEMU local)
const ui_testing::Pixel kOverlayBackgroundColor3(0x0D, 0xFF, 0x00,
                                                 0xFF);  // Green, blended with blue (AEMU infra)
const ui_testing::Pixel kOverlayBackgroundColor4(0x0D, 0xFF, 0x0D,
                                                 0xFF);  // Green, blended with pink (AEMU infra)
const ui_testing::Pixel kOverlayBackgroundColor5(0x0D, 0xFE, 0x00,
                                                 0xFF);  // Green, blended with blue (NUC)
const ui_testing::Pixel kOverlayBackgroundColor6(0x00, 0xFF, 0x0D,
                                                 0xFF);  // Green, blended with pink (NUC)

static uint32_t OverlayPixelCount(std::map<ui_testing::Pixel, uint32_t>& histogram) {
  return histogram[kOverlayBackgroundColor1] + histogram[kOverlayBackgroundColor2] +
         histogram[kOverlayBackgroundColor3] + histogram[kOverlayBackgroundColor4] +
         histogram[kOverlayBackgroundColor5] + histogram[kOverlayBackgroundColor6];
}

void FlutterEmbedderTest::SetUpRealmBase() {
  FX_LOGS(INFO) << "Setting up realm base.";

  // Add test UI stack component.
  realm_builder_.AddChild(kTestUIStack, GetParam());

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
                             Protocol{fuchsia::ui::test::scene::Controller::Name_},
                             Protocol{fuchsia::ui::scenic::Scenic::Name_},
                             Protocol{fuchsia::ui::composition::Screenshot::Name_},
                             Protocol{fuchsia::ui::display::singleton::Info::Name_}},
            .source = kTestUIStackRef,
            .targets = {ParentRef{}}});
}

// Checks whether the view with |view_ref_koid| has connected to the view tree. The response of a
// f.u.o.g.Provider.Watch call is stored in |watch_response| if it contains |view_ref_koid|.
bool FlutterEmbedderTest::HasViewConnected(
    const fuchsia::ui::observation::geometry::ViewTreeWatcherPtr& view_tree_watcher,
    std::optional<fuchsia::ui::observation::geometry::WatchResponse>& watch_response,
    zx_koid_t view_ref_koid) {
  std::optional<fuchsia::ui::observation::geometry::WatchResponse> view_tree_result;
  view_tree_watcher->Watch(
      [&view_tree_result](auto response) { view_tree_result = std::move(response); });
  FX_LOGS(INFO) << "Waiting for view tree result";
  RunLoopUntil([&view_tree_result] { return view_tree_result.has_value(); });
  FX_LOGS(INFO) << "Received view tree result";
  if (CheckViewExistsInUpdates(view_tree_result->updates(), view_ref_koid)) {
    watch_response = std::move(view_tree_result);
  };
  return watch_response.has_value();
}

void FlutterEmbedderTest::BuildRealmAndLaunchApp(const std::string& component_url,
                                                 const std::vector<std::string>& component_args,
                                                 bool usePointerInjection2) {
  FX_LOGS(INFO) << "Building realm with component: " << component_url;
  realm_builder_.AddChild(kParentFlutterRealm, kParentViewUrl);

  // Capabilities routed to embedded flutter app.
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                .source = kTestUIStackRef,
                                .targets = {kParentFlutterRealmRef}});

  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::pointerinjector::Registry::Name_}},
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

  // Construct a args.csv file containing the specified comma-separated component args.
  std::string csv;
  for (const auto& arg : component_args) {
    csv += arg + ',';
  }

  if (usePointerInjection2) {
    csv += kUsePointerInjection2Args;
  } else {
    if (csv.length() > 0) {
      // Remove last comma.
      csv.pop_back();
    }
  }

  if (csv.length() > 0) {
    // Route the /config/data to the parent view.
    auto config_directory_contents = DirectoryContents();

    config_directory_contents.AddFile("args.csv", csv);

    if (usePointerInjection2) {
      config_directory_contents.AddFile("flutter_runner_config", GetPointerInjectorArgs());
    }

    realm_builder_.RouteReadOnlyDirectory("config-data", {kParentFlutterRealmRef},
                                          std::move(config_directory_contents));
  }

  realm_ = std::make_unique<RealmRoot>(realm_builder_.Build());

  // Get the display information using the |fuchsia.ui.display.singleton.Info|.
  std::optional<bool> has_completed;
  fuchsia::ui::display::singleton::InfoPtr display_info =
      realm_->Connect<fuchsia::ui::display::singleton::Info>();
  display_info->GetMetrics([this, &has_completed](auto info) {
    display_width_ = info.extent_in_px().width;
    display_height_ = info.extent_in_px().height;
    has_completed = true;
  });

  screenshotter_ = realm_->Connect<fuchsia::ui::composition::Screenshot>();

  RunLoopUntil([&has_completed] { return has_completed.has_value(); });

  FX_LOGS(INFO) << "Got display_width " << display_width_ << " display_height " << display_height_;

  // Register fake touch screen device.
  RegisterTouchScreen();

  // Instruct Root Presenter to present test's View.
  std::optional<zx_koid_t> view_ref_koid;
  scene_provider_ = realm_->Connect<fuchsia::ui::test::scene::Controller>();
  scene_provider_.set_error_handler(
      [](auto) { FX_LOGS(ERROR) << "Error from test scene provider"; });
  fuchsia::ui::test::scene::ControllerAttachClientViewRequest request;
  request.set_view_provider(realm_->Connect<fuchsia::ui::app::ViewProvider>());
  scene_provider_->RegisterViewTreeWatcher(view_tree_watcher_.NewRequest(), []() {});
  scene_provider_->AttachClientView(
      std::move(request),
      [&view_ref_koid](auto client_view_ref_koid) { view_ref_koid = client_view_ref_koid; });

  FX_LOGS(INFO) << "Waiting for client view ref koid";
  RunLoopUntil([&view_ref_koid] { return view_ref_koid.has_value(); });

  // Wait for the client view to get attached to the view tree.
  std::optional<fuchsia::ui::observation::geometry::WatchResponse> watch_response;
  FX_LOGS(INFO) << "Waiting for client view to render";
  RunLoopUntil([this, &watch_response, &view_ref_koid] {
    return HasViewConnected(view_tree_watcher_, watch_response, *view_ref_koid);
  });
  FX_LOGS(INFO) << "Client view has rendered";

  scenic_ = realm_->Connect<fuchsia::ui::scenic::Scenic>();
  FX_LOGS(INFO) << "Launched component: " << component_url;
}

void FlutterEmbedderTest::RegisterTouchScreen() {
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

void FlutterEmbedderTest::InjectTap(int32_t x, int32_t y) {
  fuchsia::ui::test::input::TouchScreenSimulateTapRequest tap_request;
  tap_request.mutable_tap_location()->x = x;
  tap_request.mutable_tap_location()->y = y;
  fake_touchscreen_->SimulateTap(std::move(tap_request), [x, y]() {
    FX_LOGS(INFO) << "Tap injected at (" << x << ", " << y << ")";
  });
}

void FlutterEmbedderTest::TryInject(int32_t x, int32_t y) {
  InjectTap(x, y);
  async::PostDelayedTask(
      dispatcher(), [this, x, y] { TryInject(x, y); }, kTapRetryInterval);
}

std::string FlutterEmbedderTest::GetPointerInjectorArgs() {
  std::ostringstream config;

  config << "{"
         << "   \"intercept_all_input\" : true"
         << "}";

  return config.str();
}

INSTANTIATE_TEST_SUITE_P(
    FlutterEmbedderTestWithParams, FlutterEmbedderTest,
    ::testing::Values(
        "fuchsia-pkg://fuchsia.com/gfx-root-presenter-test-ui-stack#meta/test-ui-stack.cm",
        "fuchsia-pkg://fuchsia.com/gfx-scene-manager-test-ui-stack#meta/test-ui-stack.cm"));

TEST_P(FlutterEmbedderTest, Embedding) {
  BuildRealmAndLaunchApp(kParentViewUrl);

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(
      kChildBackgroundColor, [](std::map<ui_testing::Pixel, uint32_t> histogram) {
        // Expect parent and child background colors, with parent color > child color.
        EXPECT_GT(histogram[kParentBackgroundColor], 0u);
        EXPECT_GT(histogram[kChildBackgroundColor], 0u);
        EXPECT_GT(histogram[kParentBackgroundColor], histogram[kChildBackgroundColor]);
      }));
}

TEST_P(FlutterEmbedderTest, HittestEmbedding) {
  BuildRealmAndLaunchApp(kParentViewUrl);

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Simulate a tap at the center of the child view.
  TryInject(/* x = */ 0, /* y = */ 0);

  // Take screenshot until we see the child-view's tapped color.
  ASSERT_TRUE(
      TakeScreenshotUntil(kChildTappedColor, [](std::map<ui_testing::Pixel, uint32_t> histogram) {
        // Expect parent and child background colors, with parent color > child color.
        EXPECT_GT(histogram[kParentBackgroundColor], 0u);
        EXPECT_EQ(histogram[kChildBackgroundColor], 0u);
        EXPECT_GT(histogram[kChildTappedColor], 0u);
        EXPECT_GT(histogram[kParentBackgroundColor], histogram[kChildTappedColor]);
      }));
}

TEST_P(FlutterEmbedderTest, HittestDisabledEmbedding) {
  BuildRealmAndLaunchApp(kParentViewUrl, {"--no-hitTestable"});

  // Take screenshots until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Simulate a tap at the center of the child view.
  TryInject(/* x = */ 0, /* y = */ 0);

  // The parent-view should change color.
  ASSERT_TRUE(
      TakeScreenshotUntil(kParentTappedColor, [](std::map<ui_testing::Pixel, uint32_t> histogram) {
        // Expect parent and child background colors, with parent color > child color.
        EXPECT_EQ(histogram[kParentBackgroundColor], 0u);
        EXPECT_GT(histogram[kParentTappedColor], 0u);
        EXPECT_GT(histogram[kChildBackgroundColor], 0u);
        EXPECT_EQ(histogram[kChildTappedColor], 0u);
        EXPECT_GT(histogram[kParentTappedColor], histogram[kChildBackgroundColor]);
      }));
}

TEST_P(FlutterEmbedderTest, EmbeddingWithOverlay) {
  BuildRealmAndLaunchApp(kParentViewUrl, {"--showOverlay"});

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(
      kChildBackgroundColor, [](std::map<ui_testing::Pixel, uint32_t> histogram) {
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

TEST_P(FlutterEmbedderTest, HittestEmbeddingWithOverlay) {
  BuildRealmAndLaunchApp(kParentViewUrl, {"--showOverlay"});

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // The bottom-left corner of the overlay is at the center of the screen,
  // which is at (0, 0) in the injection coordinate space. Inject a pointer
  // event just outside the overlay's bounds, and ensure that it goes to the
  // embedded view.
  TryInject(/* x = */ -1, /* y = */ 1);

  // Take screenshot until we see the child-view's tapped color.
  ASSERT_TRUE(
      TakeScreenshotUntil(kChildTappedColor, [](std::map<ui_testing::Pixel, uint32_t> histogram) {
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

TEST_P(FlutterEmbedderTest, ChildViewReinjectionTest) {
  BuildRealmAndLaunchApp(kParentViewUrl, {}, true);

  // Take screenshot until we see the child-view's embedded color.
  ASSERT_TRUE(TakeScreenshotUntil(kChildBackgroundColor));

  // Simulate a tap at the center of the child view.
  TryInject(/* x = */ 0, /* y = */ 0);

  // Take screenshot until we see the child-view's tapped color.
  ASSERT_TRUE(
      TakeScreenshotUntil(kChildTappedColor, [](std::map<ui_testing::Pixel, uint32_t> histogram) {
        // Expect parent and child background colors, with parent color > child color.
        EXPECT_GT(histogram[kParentBackgroundColor], 0u);
        EXPECT_EQ(histogram[kChildBackgroundColor], 0u);
        EXPECT_GT(histogram[kChildTappedColor], 0u);
        EXPECT_GT(histogram[kParentBackgroundColor], histogram[kChildTappedColor]);
      }));
}

}  // namespace flutter_embedder_test
