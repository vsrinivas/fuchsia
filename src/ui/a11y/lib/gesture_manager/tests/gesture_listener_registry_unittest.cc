// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_listener_registry.h"

#include <fuchsia/accessibility/gesture/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {
namespace {

class MockGestureListener : public fuchsia::accessibility::gesture::Listener {
 public:
  MockGestureListener() : binding_(this) {
    binding_.set_error_handler([this](zx_status_t) { is_registered_ = false; });
  }

  fidl::InterfaceHandle<fuchsia::accessibility::gesture::Listener> NewBinding() {
    is_registered_ = true;
    return binding_.NewBinding();
  }

  bool is_registered() const { return is_registered_; }

 private:
  void OnGesture(fuchsia::accessibility::gesture::Type gesture_type,
                 OnGestureCallback callback) override {
    callback(true, nullptr);
  }

  fidl::Binding<fuchsia::accessibility::gesture::Listener> binding_;
  bool is_registered_ = false;
};

class GestureListenerRegistryTest : public gtest::TestLoopFixture {
 public:
  GestureListenerRegistryTest() = default;
  ~GestureListenerRegistryTest() override = default;

  a11y::GestureListenerRegistry registry_;
};

TEST_F(GestureListenerRegistryTest, RegistersSuccessfully) {
  EXPECT_FALSE(registry_.listener());
  MockGestureListener listener;
  registry_.Register(listener.NewBinding(), []() {});
  EXPECT_TRUE(registry_.listener());
}

TEST_F(GestureListenerRegistryTest, HonorsLastRegisteredListener) {
  EXPECT_FALSE(registry_.listener());
  MockGestureListener listener;
  MockGestureListener last_listener;
  registry_.Register(listener.NewBinding(), []() {});
  registry_.Register(last_listener.NewBinding(), []() {});
  RunLoopUntilIdle();
  EXPECT_TRUE(registry_.listener());
  EXPECT_TRUE(last_listener.is_registered());
  EXPECT_FALSE(listener.is_registered());
}

}  // namespace
}  // namespace accessibility_test
