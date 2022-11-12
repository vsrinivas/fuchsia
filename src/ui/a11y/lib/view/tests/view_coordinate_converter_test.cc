// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_coordinate_converter.h"

#include <fuchsia/ui/observation/geometry/cpp/fidl.h>
#include <fuchsia/ui/observation/scope/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace accessibility_test {
namespace {

using a11y::ViewCoordinateConverter;

ViewCoordinateConverter MakeConverter(sys::ComponentContext* component_context,
                                      zx_koid_t context_view_ref_koid) {
  return ViewCoordinateConverter{
      component_context->svc()->Connect<fuchsia::ui::observation::scope::Registry>(),
      context_view_ref_koid};
}

// Helper method to return a valid response.
fuchsia::ui::observation::geometry::WatchResponse BuildDefaultResponse() {
  fuchsia::ui::observation::geometry::WatchResponse response;
  auto* updates = response.mutable_updates();
  fuchsia::ui::observation::geometry::ViewTreeSnapshot snapshot;
  fuchsia::ui::observation::geometry::ViewDescriptor context_view;
  context_view.set_view_ref_koid(1u);
  context_view.mutable_extent_in_context()->origin.x = 0;
  context_view.mutable_extent_in_context()->origin.y = 0;
  context_view.mutable_layout()->extent.min.x = 0;
  context_view.mutable_layout()->extent.min.y = 0;
  context_view.mutable_layout()->extent.max.x = 10;
  context_view.mutable_layout()->extent.max.y = 10;
  context_view.mutable_extent_in_context()->angle_degrees = 0;
  context_view.mutable_extent_in_context()->width = 10;
  context_view.mutable_extent_in_context()->height = 10;

  fuchsia::ui::observation::geometry::ViewDescriptor client_view;
  client_view.set_view_ref_koid(2u);
  client_view.mutable_extent_in_context()->origin.x = 2;
  client_view.mutable_extent_in_context()->origin.y = 2;
  client_view.mutable_layout()->extent.min.x = 0;
  client_view.mutable_layout()->extent.min.y = 0;
  client_view.mutable_layout()->extent.max.x = 5;
  client_view.mutable_layout()->extent.max.y = 5;
  client_view.mutable_extent_in_context()->angle_degrees = 0;
  client_view.mutable_extent_in_context()->width = 5;
  client_view.mutable_extent_in_context()->height = 5;

  snapshot.mutable_views()->push_back(std::move(context_view));
  snapshot.mutable_views()->push_back(std::move(client_view));

  updates->push_back(std::move(snapshot));
  return response;
}

// A mock for the geometry retistry service. This object also answers calls to the watcher service.
class MockRegistry : public fuchsia::ui::observation::scope::Registry,
                     public fuchsia::ui::observation::geometry::ViewTreeWatcher {
 public:
  MockRegistry() : binding_(this) {}
  ~MockRegistry() = default;

  fidl::InterfaceRequestHandler<fuchsia::ui::observation::scope::Registry> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this,
            dispatcher](fidl::InterfaceRequest<fuchsia::ui::observation::scope::Registry> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  void SetWatchResponse(fuchsia::ui::observation::geometry::WatchResponse response) {
    response_ = std::move(response);
  }

  void ReturnWatchResponse() { callback_(std::move(response_)); }

 private:
  // |fuchsia::ui::observation::scope::Registry|
  void RegisterScopedViewTreeWatcher(
      uint64_t context_view,
      ::fidl::InterfaceRequest<::fuchsia::ui::observation::geometry::ViewTreeWatcher> geometry,
      RegisterScopedViewTreeWatcherCallback callback) override {
    binding_.Bind(std::move(geometry));
    callback();
  }

  // |fuchsia::ui::observation::geometry::ViewTreeWatcher|
  void Watch(WatchCallback callback) override { callback_ = std::move(callback); }

  fuchsia::ui::observation::geometry::WatchResponse response_;
  WatchCallback callback_;
  fidl::BindingSet<fuchsia::ui::observation::scope::Registry> bindings_;
  fidl::Binding<fuchsia::ui::observation::geometry::ViewTreeWatcher> binding_;
};

class ViewCoordinateConverterTest : public gtest::TestLoopFixture {
 public:
  ViewCoordinateConverterTest() = default;
  ~ViewCoordinateConverterTest() = default;

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    context_provider_.service_directory_provider()->AddService(mock_registry_.GetHandler());
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  MockRegistry mock_registry_;
};

TEST_F(ViewCoordinateConverterTest, ResponseHasError) {
  ViewCoordinateConverter converter = MakeConverter(context_provider_.context(), 1u);
  auto response = BuildDefaultResponse();
  response.set_error(fuchsia::ui::observation::geometry::Error::VIEWS_OVERFLOW);
  mock_registry_.SetWatchResponse(std::move(response));

  RunLoopUntilIdle();

  // Response is not received, check no conversion can occur.
  EXPECT_FALSE(converter.Convert(1u, {1, 1}));

  mock_registry_.ReturnWatchResponse();

  RunLoopUntilIdle();

  // Response contains an error.
  EXPECT_FALSE(converter.Convert(1u, {1, 1}));
}

TEST_F(ViewCoordinateConverterTest, DiscardResponsesWithNoUpdates) {
  ViewCoordinateConverter converter = MakeConverter(context_provider_.context(), 1u);
  auto response = BuildDefaultResponse();
  response.clear_updates();
  mock_registry_.SetWatchResponse(std::move(response));

  RunLoopUntilIdle();

  mock_registry_.ReturnWatchResponse();

  RunLoopUntilIdle();

  EXPECT_FALSE(converter.Convert(1u, {1, 1}));
}

TEST_F(ViewCoordinateConverterTest, ConvertsAngleZeroClientViewCoordinate) {
  ViewCoordinateConverter converter = MakeConverter(context_provider_.context(), 1u);
  auto response = BuildDefaultResponse();
  mock_registry_.SetWatchResponse(std::move(response));

  RunLoopUntilIdle();

  mock_registry_.ReturnWatchResponse();

  RunLoopUntilIdle();

  auto coordinate = converter.Convert(2u, {1, 2});
  ASSERT_TRUE(coordinate);
  EXPECT_FLOAT_EQ(coordinate->x, 3.0);
  EXPECT_FLOAT_EQ(coordinate->y, 4.0);
}

TEST_F(ViewCoordinateConverterTest, ConvertsAngleNinetyClientViewCoordinate) {
  ViewCoordinateConverter converter = MakeConverter(context_provider_.context(), 1u);
  auto response = BuildDefaultResponse();
  response.mutable_updates()
      ->back()
      .mutable_views()
      ->back()
      .mutable_extent_in_context()
      ->angle_degrees = 90;
  mock_registry_.SetWatchResponse(std::move(response));

  RunLoopUntilIdle();

  mock_registry_.ReturnWatchResponse();

  RunLoopUntilIdle();

  auto coordinate = converter.Convert(2u, {1, 2});
  ASSERT_TRUE(coordinate);
  EXPECT_FLOAT_EQ(coordinate->x, 4.0);
  EXPECT_FLOAT_EQ(coordinate->y, 1.0);
}

TEST_F(ViewCoordinateConverterTest, ConvertsAngleOneHundredAndEightyClientViewCoordinate) {
  ViewCoordinateConverter converter = MakeConverter(context_provider_.context(), 1u);
  auto response = BuildDefaultResponse();
  response.mutable_updates()
      ->back()
      .mutable_views()
      ->back()
      .mutable_extent_in_context()
      ->angle_degrees = 180;
  mock_registry_.SetWatchResponse(std::move(response));

  RunLoopUntilIdle();

  mock_registry_.ReturnWatchResponse();

  RunLoopUntilIdle();

  auto coordinate = converter.Convert(2u, {1, 2});
  ASSERT_TRUE(coordinate);
  EXPECT_FLOAT_EQ(coordinate->x, 1.0);
  EXPECT_FLOAT_EQ(coordinate->y, 0.0);
}

TEST_F(ViewCoordinateConverterTest, ConvertsAngleTwoHundredAndSeventyClientViewCoordinate) {
  ViewCoordinateConverter converter = MakeConverter(context_provider_.context(), 1u);
  auto response = BuildDefaultResponse();
  response.mutable_updates()
      ->back()
      .mutable_views()
      ->back()
      .mutable_extent_in_context()
      ->angle_degrees = 270;
  mock_registry_.SetWatchResponse(std::move(response));

  RunLoopUntilIdle();

  mock_registry_.ReturnWatchResponse();

  RunLoopUntilIdle();

  auto coordinate = converter.Convert(2u, {1, 2});
  ASSERT_TRUE(coordinate);
  EXPECT_FLOAT_EQ(coordinate->x, 0.0);
  EXPECT_FLOAT_EQ(coordinate->y, 3.0);
}

TEST_F(ViewCoordinateConverterTest, ConvertsClientViewWithScale) {
  ViewCoordinateConverter converter = MakeConverter(context_provider_.context(), 1u);
  auto response = BuildDefaultResponse();

  // Set a different client view width and height (default = 5)  in  the context view. This results
  // in an implicit scaling factor that is applied. Thus, 10/ 5 = 2 -> new scaling factor.
  auto* extent =
      response.mutable_updates()->back().mutable_views()->back().mutable_extent_in_context();
  extent->width = 10.0;
  extent->height = 10.0;

  mock_registry_.SetWatchResponse(std::move(response));

  RunLoopUntilIdle();

  mock_registry_.ReturnWatchResponse();

  RunLoopUntilIdle();

  auto coordinate = converter.Convert(2u, {1, 2});
  ASSERT_TRUE(coordinate);
  EXPECT_FLOAT_EQ(coordinate->x, 4.0);
  EXPECT_FLOAT_EQ(coordinate->y, 6.0);
}

TEST_F(ViewCoordinateConverterTest, NotifiesRegisteredClientsAboutChangesInGeometry) {
  ViewCoordinateConverter converter = MakeConverter(context_provider_.context(), 1u);
  bool callback_called = false;
  converter.RegisterCallback([&callback_called]() { callback_called = true; });
  auto response = BuildDefaultResponse();
  mock_registry_.SetWatchResponse(std::move(response));

  RunLoopUntilIdle();

  EXPECT_FALSE(callback_called);
  mock_registry_.ReturnWatchResponse();

  RunLoopUntilIdle();

  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace accessibility_test
