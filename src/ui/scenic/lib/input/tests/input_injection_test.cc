// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/tests/util.h"

namespace lib_ui_input_tests {
namespace {

class InputInjectionTest : public InputSystemTest {
 public:
  InputInjectionTest() {}

 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }
};

TEST_F(InputInjectionTest, RegisterAttemptWithCorrectArguments_ShouldSucceed) {
  auto [view_ref_control1, context_view_ref] = scenic::ViewRefPair::New();
  auto [view_ref_control2, target_view_ref] = scenic::ViewRefPair::New();

  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool register_callback_fired = false;
  bool error_callback_fired = false;
  injector.set_error_handler(
      [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
  {
    fuchsia::ui::pointerflow::InjectorConfig config;
    {
      fuchsia::ui::pointerflow::DeviceConfig device_config;
      device_config.set_device_id(1);
      device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
      config.set_device_config(std::move(device_config));
    }
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(context_view_ref));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(target_view_ref));
      config.set_target(std::move(target));
    }
    config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
  }

  RunLoopUntilIdle();

  EXPECT_TRUE(register_callback_fired);
  EXPECT_FALSE(error_callback_fired);
}

TEST_F(InputInjectionTest, RegisterAttemptWithBadDeviceConfig_ShouldFail) {
  auto [view_ref_control1, context_view_ref] = scenic::ViewRefPair::New();
  auto [view_ref_control2, target_view_ref] = scenic::ViewRefPair::New();

  fuchsia::ui::pointerflow::InjectorPtr injector;

  fuchsia::ui::pointerflow::InjectorConfig base_config;
  {
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(context_view_ref));
      base_config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(target_view_ref));
      base_config.set_target(std::move(target));
    }
    base_config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);
  }

  {  // No device config.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No device id.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
    config.set_device_config(std::move(device_config));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No device type.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    device_config.set_device_id(1);
    config.set_device_config(std::move(device_config));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No device type.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    device_config.set_device_id(1);
    // Set to not TOUCH.
    device_config.set_device_type(static_cast<fuchsia::ui::input3::PointerDeviceType>(
        static_cast<uint32_t>(fuchsia::ui::input3::PointerDeviceType::TOUCH) + 1));
    config.set_device_config(std::move(device_config));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(InputInjectionTest, RegisterAttemptWithBadContextOrTarget_ShouldFail) {
  auto [view_ref_control1, context_view_ref] = scenic::ViewRefPair::New();
  auto [view_ref_control2, target_view_ref] = scenic::ViewRefPair::New();

  fuchsia::ui::pointerflow::InjectorPtr injector;

  fuchsia::ui::pointerflow::InjectorConfig base_config;
  {
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    {
      device_config.set_device_id(1);
      device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
      base_config.set_device_config(std::move(device_config));
    }
    base_config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);
  }

  {  // No context.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
    fuchsia::ui::pointerflow::InjectorTarget target;
    target.set_view(std::move(view_ref));
    config.set_target(std::move(target));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No target.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
    fuchsia::ui::pointerflow::InjectorContext context;
    context.set_view(std::move(view_ref));
    config.set_context(std::move(context));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Context equals target.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
    fuchsia::ui::views::ViewRef view_ref_clone;
    fidl::Clone(view_ref, &view_ref_clone);
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(view_ref));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(view_ref_clone));
      config.set_target(std::move(target));
    }

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(InputInjectionTest, RegisterAttemptWithBadDispatchPolicy_ShouldFail) {
  auto [view_ref_control1, context_view_ref] = scenic::ViewRefPair::New();
  auto [view_ref_control2, target_view_ref] = scenic::ViewRefPair::New();

  fuchsia::ui::pointerflow::InjectorPtr injector;

  fuchsia::ui::pointerflow::InjectorConfig base_config;
  {
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    device_config.set_device_id(1);
    device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
    base_config.set_device_config(std::move(device_config));
  }
  {
    fuchsia::ui::pointerflow::InjectorContext context;
    context.set_view(std::move(context_view_ref));
    base_config.set_context(std::move(context));
  }
  {
    fuchsia::ui::pointerflow::InjectorTarget target;
    target.set_view(std::move(target_view_ref));
    base_config.set_target(std::move(target));
  }

  {  // No dispatch policy.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Unallowed dispatch policy.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::ALL_HIT_PARALLEL);

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(InputInjectionTest, ChannelDying_ShouldNotCrash) {
  auto [view_ref_control1, context_view_ref] = scenic::ViewRefPair::New();
  auto [view_ref_control2, target_view_ref] = scenic::ViewRefPair::New();

  {
    fuchsia::ui::pointerflow::InjectorPtr injector;

    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
    {
      fuchsia::ui::pointerflow::InjectorConfig config;
      {
        fuchsia::ui::pointerflow::DeviceConfig device_config;
        device_config.set_device_id(1);
        device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
        config.set_device_config(std::move(device_config));
      }
      {
        fuchsia::ui::pointerflow::InjectorContext context;
        context.set_view(std::move(context_view_ref));
        config.set_context(std::move(context));
      }
      {
        fuchsia::ui::pointerflow::InjectorTarget target;
        target.set_view(std::move(target_view_ref));
        config.set_target(std::move(target));
      }
      config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);

      input_system()->Register(std::move(config), injector.NewRequest(),
                               [&register_callback_fired] { register_callback_fired = true; });
    }

    RunLoopUntilIdle();

    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }  // |injector| goes out of scope.

  RunLoopUntilIdle();
}

TEST_F(InputInjectionTest, MultipleRegistrations_ShouldSucceed) {
  auto [view_ref_control1, context_view_ref] = scenic::ViewRefPair::New();
  auto [view_ref_control2, target_view_ref] = scenic::ViewRefPair::New();

  fuchsia::ui::pointerflow::InjectorConfig config;
  {
    {
      fuchsia::ui::pointerflow::DeviceConfig device_config;
      device_config.set_device_id(1);
      device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
      config.set_device_config(std::move(device_config));
    }
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(context_view_ref));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(target_view_ref));
      config.set_target(std::move(target));
    }
    config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);
  }
  fuchsia::ui::pointerflow::InjectorConfig config2;
  fidl::Clone(config, &config2);

  fuchsia::ui::pointerflow::InjectorPtr injector;
  {
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }

  fuchsia::ui::pointerflow::InjectorPtr injector2;
  {
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector2.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
    input_system()->Register(std::move(config2), injector2.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
