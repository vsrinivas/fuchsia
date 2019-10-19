// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_manager2.h"

#include <fuchsia/ui/display/cpp/fidl.h>
#include <fuchsia/ui/display/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/gtest/test_loop_fixture.h>
#include <zircon/pixelformat.h>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/display/display_controller_listener.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"

namespace scenic_impl {
namespace display {
namespace test {

namespace {

struct DisplayControllerObjects {
  std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> interface_ptr;
  std::unique_ptr<MockDisplayController> mock;
  std::unique_ptr<DisplayControllerListener> listener;
};

DisplayControllerObjects CreateMockDisplayController() {
  DisplayControllerObjects controller_objs;

  zx::channel device_channel_server;
  zx::channel device_channel_client;
  FXL_CHECK(ZX_OK == zx::channel::create(0, &device_channel_server, &device_channel_client));
  zx::channel controller_channel_server;
  zx::channel controller_channel_client;
  FXL_CHECK(ZX_OK ==
            zx::channel::create(0, &controller_channel_server, &controller_channel_client));

  controller_objs.mock = std::make_unique<MockDisplayController>();
  controller_objs.mock->Bind(std::move(device_channel_server),
                             std::move(controller_channel_server));

  zx_handle_t controller_handle = controller_channel_client.get();
  controller_objs.interface_ptr = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
  controller_objs.interface_ptr->Bind(std::move(controller_channel_client));
  controller_objs.listener = std::make_unique<DisplayControllerListener>(
      std::move(device_channel_client), controller_objs.interface_ptr, controller_handle);

  return controller_objs;
}

}  // namespace

using DisplayManagerTest = gtest::TestLoopFixture;

using OnDisplayAddedCallback = std::function<void(fuchsia::ui::display::Info)>;
using OnDisplayRemovedCallback = std::function<void(fuchsia::ui::display::DisplayRef)>;
using OnDisplayOwnershipChangedCallback = std::function<void(
    std::vector<fuchsia::ui::display::DisplayRef> displays, bool owned_by_display_controller)>;

class MockDisplayListener : public fuchsia::ui::display::testing::DisplayListener_TestBase {
 public:
  void NotImplemented_(const std::string& name) final {}

  fidl::InterfaceHandle<fuchsia::ui::display::DisplayListener> Bind() {
    fidl::InterfaceHandle<fuchsia::ui::display::DisplayListener> listener;
    bindings_.AddBinding(this, listener.NewRequest(), nullptr);
    return listener;
  }

  void set_on_display_added_callback(OnDisplayAddedCallback callback) {
    on_display_added_cb_ = callback;
  }

  void set_on_display_removed_callback(OnDisplayRemovedCallback callback) {
    on_display_removed_cb_ = callback;
  }

  void set_on_display_ownership_changed_callback(OnDisplayOwnershipChangedCallback callback) {
    on_display_ownership_changed_cb_ = callback;
  }

  void OnDisplayAdded(fuchsia::ui::display::Info display) {
    if (on_display_added_cb_) {
      on_display_added_cb_(std::move(display));
    }
  }

  void OnDisplayRemoved(fuchsia::ui::display::DisplayRef display) {
    if (on_display_removed_cb_) {
      on_display_removed_cb_(std::move(display));
    }
  }

  void OnDisplayOwnershipChanged(std::vector<fuchsia::ui::display::DisplayRef> displays,
                                 bool owned_by_display_controller) {
    if (on_display_ownership_changed_cb_) {
      on_display_ownership_changed_cb_(std::move(displays), owned_by_display_controller);
    }
  }

 private:
  fidl::BindingSet<fuchsia::ui::display::DisplayListener> bindings_;
  OnDisplayAddedCallback on_display_added_cb_;
  OnDisplayRemovedCallback on_display_removed_cb_;
  OnDisplayOwnershipChangedCallback on_display_ownership_changed_cb_;
};

static fuchsia::hardware::display::Info CreateFakeDisplayInfo(uint64_t display_id) {
  fuchsia::hardware::display::Mode mode;
  mode.horizontal_resolution = 1024;
  mode.vertical_resolution = 800;
  mode.refresh_rate_e2 = 60;
  mode.flags = 0;
  fuchsia::hardware::display::Info display;
  display.id = display_id;
  display.modes = {mode};
  display.pixel_format = {ZX_PIXEL_FORMAT_ARGB_8888};
  display.cursor_configs = {};
  display.manufacturer_name = "fake_manufacturer_name";
  display.monitor_name = "fake_monitor_name";
  display.monitor_serial = "fake_monitor_serial";
  return display;
}

TEST_F(DisplayManagerTest, RemoveInvalidDisplayController) {
  DisplayManager2 display_manager;
  DisplayControllerObjects display_controller_objs = CreateMockDisplayController();
  display_manager.AddDisplayController(display_controller_objs.interface_ptr,
                                       std::move(display_controller_objs.listener));

  std::vector<fuchsia::ui::display::Info> displays_added;
  std::vector<fuchsia::ui::display::DisplayRef> displays_removed;

  MockDisplayListener displays_changed_listener;
  display_manager.AddDisplayListener(displays_changed_listener.Bind());
  displays_changed_listener.set_on_display_added_callback(
      [&displays_added](auto display_info) { displays_added.push_back(std::move(display_info)); });
  displays_changed_listener.set_on_display_removed_callback([&displays_removed](auto display_ref) {
    displays_removed.push_back(std::move(display_ref));
  });

  // Add display with id = 1
  display_controller_objs.mock->events().DisplaysChanged(
      /* added */ {CreateFakeDisplayInfo(/*display_id=*/1)},
      /* removed */ {});
  RunLoopUntilIdle();
  ASSERT_EQ(1u, displays_added.size());

  // Invalidate display controller.
  display_controller_objs.mock.reset();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, displays_added.size());  // Unchanged.

  // Displays are marked as removed if their display controller is destroyed.
  EXPECT_EQ(1u, displays_removed.size());
}

TEST_F(DisplayManagerTest, DisplaysChanged) {
  DisplayControllerObjects display_controller_objs = CreateMockDisplayController();
  {
    DisplayManager2 display_manager;
    display_manager.AddDisplayController(display_controller_objs.interface_ptr,
                                         std::move(display_controller_objs.listener));
    std::vector<fuchsia::ui::display::Info> displays_added;
    std::vector<fuchsia::ui::display::DisplayRef> displays_removed;

    MockDisplayListener displays_changed_listener;
    display_manager.AddDisplayListener(displays_changed_listener.Bind());
    displays_changed_listener.set_on_display_added_callback([&displays_added](auto display_info) {
      displays_added.push_back(std::move(display_info));
    });
    displays_changed_listener.set_on_display_removed_callback(
        [&displays_removed](auto display_ref) {
          displays_removed.push_back(std::move(display_ref));
        });

    // Add display with id = 1
    display_controller_objs.mock->events().DisplaysChanged(
        /* added */ {CreateFakeDisplayInfo(/*display_id=*/1)},
        /* removed */ {});
    RunLoopUntilIdle();
    ASSERT_EQ(1u, displays_added.size());
    EXPECT_EQ(0u, displays_removed.size());

    // Add another display with id = 1. Expect error.
    display_controller_objs.mock->events().DisplaysChanged(
        /* added */ {CreateFakeDisplayInfo(/*display_id=*/1)},
        /* removed */ {});
    RunLoopUntilIdle();
    EXPECT_EQ(display_manager.last_error(),
              "DisplayManager: Display added, but a display already exists with same id=1");
    EXPECT_EQ(1u, displays_added.size());
    EXPECT_EQ(0u, displays_removed.size());

    // Remove display that doesn't exist.
    display_controller_objs.mock->events().DisplaysChanged(/* added */ {}, /* removed */ {2u});
    RunLoopUntilIdle();
    EXPECT_EQ(display_manager.last_error(),
              "DisplayManager: Got a display removed event for invalid display=2");
    ASSERT_EQ(1u, displays_added.size());
    EXPECT_EQ(0u, displays_removed.size());

    // Remove display that exists.
    display_controller_objs.mock->events().DisplaysChanged(/* added */ {}, /* removed */ {1u});
    RunLoopUntilIdle();
    EXPECT_EQ(1u, displays_added.size());
    EXPECT_EQ(1u, displays_removed.size());

    // Add display with id = 2
    display_controller_objs.mock->events().DisplaysChanged(
        /* added */ {CreateFakeDisplayInfo(/*display_id=*/2)},
        /* removed */ {});
    RunLoopUntilIdle();
    ASSERT_EQ(2u, displays_added.size());
    EXPECT_EQ(1u, displays_removed.size());

    // The two displays are unique.
    EXPECT_NE(fsl::GetKoid(displays_added[0].display_ref().reference.get()),
              fsl::GetKoid(displays_added[1].display_ref().reference.get()));
  }

  // Expect no crashes during teardown.

  // Trigger display controller events after display manager is destroyed.
  display_controller_objs.mock->events().DisplaysChanged(
      /* added */ {CreateFakeDisplayInfo(/*display_id=*/3)},
      /* removed */ {});
  display_controller_objs.mock->events().ClientOwnershipChange(true);

  // Invalidate display controller.
  display_controller_objs.mock.reset();
  RunLoopUntilIdle();
}

TEST_F(DisplayManagerTest, DisplaysChangedBeforeAdddingListener) {
  DisplayManager2 display_manager;
  DisplayControllerObjects display_controller_objs = CreateMockDisplayController();
  display_manager.AddDisplayController(display_controller_objs.interface_ptr,
                                       std::move(display_controller_objs.listener));
  std::vector<fuchsia::ui::display::Info> displays_added;
  std::vector<fuchsia::ui::display::DisplayRef> displays_removed;
  std::vector<fuchsia::ui::display::DisplayRef> displays_ownership_changed;
  bool has_ownership = false;

  // Add displays with id = 1 and id = 2
  display_controller_objs.mock->events().DisplaysChanged(
      /* added */ {CreateFakeDisplayInfo(/*display_id=*/1),
                   CreateFakeDisplayInfo(/*display_id=*/2)},
      /* removed */ {});
  RunLoopUntilIdle();

  // Remove display with id = 1.
  display_controller_objs.mock->events().DisplaysChanged(/* added */ {}, /* removed */ {1u});
  RunLoopUntilIdle();

  display_controller_objs.mock->events().ClientOwnershipChange(true);
  RunLoopUntilIdle();

  // Add a listener and expect it to receive an DisplayAdded with id = 1 and a display ownership
  // changed event.
  MockDisplayListener displays_changed_listener;
  display_manager.AddDisplayListener(displays_changed_listener.Bind());
  displays_changed_listener.set_on_display_added_callback(
      [&displays_added](auto display_info) { displays_added.push_back(std::move(display_info)); });
  displays_changed_listener.set_on_display_removed_callback([&displays_removed](auto display_ref) {
    displays_removed.push_back(std::move(display_ref));
  });
  displays_changed_listener.set_on_display_ownership_changed_callback(
      [&displays_ownership_changed, &has_ownership](
          std::vector<fuchsia::ui::display::DisplayRef> displays, bool owned) {
        for (auto& display : displays) {
          displays_ownership_changed.push_back(std::move(display));
        }
        has_ownership = owned;
      });
  RunLoopUntilIdle();

  EXPECT_EQ(1u, displays_added.size());
  EXPECT_EQ(0u, displays_removed.size());

  ASSERT_EQ(1u, displays_ownership_changed.size());
  EXPECT_EQ(fsl::GetKoid(displays_added[0].display_ref().reference.get()),
            fsl::GetKoid(displays_ownership_changed[0].reference.get()));
}

TEST_F(DisplayManagerTest, DisplayOwnershipChanged) {
  DisplayManager2 display_manager;

  DisplayControllerObjects display_controller_objs1 = CreateMockDisplayController();
  display_manager.AddDisplayController(display_controller_objs1.interface_ptr,
                                       std::move(display_controller_objs1.listener));
  DisplayControllerObjects display_controller_objs2 = CreateMockDisplayController();
  display_manager.AddDisplayController(display_controller_objs2.interface_ptr,
                                       std::move(display_controller_objs2.listener));

  std::vector<fuchsia::ui::display::Info> displays_added;
  std::vector<fuchsia::ui::display::DisplayRef> displays_ownership_changed;
  bool has_ownership = false;

  MockDisplayListener displays_changed_listener;
  display_manager.AddDisplayListener(displays_changed_listener.Bind());
  displays_changed_listener.set_on_display_added_callback(
      [&displays_added](auto display_info) { displays_added.push_back(std::move(display_info)); });
  displays_changed_listener.set_on_display_ownership_changed_callback(
      [&displays_ownership_changed, &has_ownership](
          std::vector<fuchsia::ui::display::DisplayRef> displays, bool owned) {
        for (auto& display : displays) {
          displays_ownership_changed.push_back(std::move(display));
        }
        has_ownership = owned;
      });

  // Add displays with ids 1...4 from two display controllers.
  display_controller_objs1.mock->events().DisplaysChanged(
      /*added=*/{CreateFakeDisplayInfo(/*display_id=*/1)},
      /*removed=*/{});
  display_controller_objs1.mock->events().DisplaysChanged(
      /*added=*/{CreateFakeDisplayInfo(/*display_id=*/2)},
      /*removed=*/{});
  // Make sure the operations for the first display controller are finished first, because we rely
  // on the order displays are added below.
  RunLoopUntilIdle();
  display_controller_objs2.mock->events().DisplaysChanged(
      /*added=*/{CreateFakeDisplayInfo(/*display_id=*/3)},
      /*removed=*/{});
  display_controller_objs2.mock->events().DisplaysChanged(
      /*added=*/{CreateFakeDisplayInfo(/*display_id=*/4)},
      /*removed=*/{});

  display_controller_objs1.mock->events().ClientOwnershipChange(true);
  RunLoopUntilIdle();
  EXPECT_EQ(4u, displays_added.size());
  EXPECT_TRUE(has_ownership);
  EXPECT_EQ(2u, displays_ownership_changed.size());
  EXPECT_EQ(fsl::GetKoid(displays_added[0].display_ref().reference.get()),
            fsl::GetKoid(displays_ownership_changed[0].reference.get()));
  EXPECT_EQ(fsl::GetKoid(displays_added[1].display_ref().reference.get()),
            fsl::GetKoid(displays_ownership_changed[1].reference.get()));
}

}  // namespace test
}  // namespace display
}  // namespace scenic_impl
