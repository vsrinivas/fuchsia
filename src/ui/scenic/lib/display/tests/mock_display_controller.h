// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_TESTS_MOCK_DISPLAY_CONTROLLER_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_TESTS_MOCK_DISPLAY_CONTROLLER_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/display/display_controller_listener.h"

namespace scenic_impl {
namespace display {
namespace test {

class MockDisplayController;

struct DisplayControllerObjects {
  std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> interface_ptr;
  std::unique_ptr<MockDisplayController> mock;
  std::unique_ptr<DisplayControllerListener> listener;
};

DisplayControllerObjects CreateMockDisplayController();

class MockDisplayController : public fuchsia::hardware::display::testing::Controller_TestBase {
 public:
  using CheckConfigFn =
      std::function<void(bool, fuchsia::hardware::display::ConfigResult*,
                         std::vector<fuchsia::hardware::display::ClientCompositionOp>*)>;
  using SetDisplayColorConversionFn = std::function<void(
      uint64_t, std::array<float, 3>, std::array<float, 9>, std::array<float, 3>)>;
  using ImportEventFn = std::function<void(zx::event event, uint64_t event_id)>;

  MockDisplayController() : binding_(this) {}

  void NotImplemented_(const std::string& name) final {}

  void WaitForMessage() { binding_.WaitForMessage(); }

  void Bind(zx::channel device_channel, zx::channel controller_channel,
            async_dispatcher_t* dispatcher = nullptr) {
    device_channel_ = std::move(device_channel);
    binding_.Bind(fidl::InterfaceRequest<fuchsia::hardware::display::Controller>(
                      std::move(controller_channel)),
                  dispatcher);
  }

  void set_import_event_fn(ImportEventFn fn) { import_event_fn_ = fn; }

  void ImportEvent(zx::event event, uint64_t event_id) override {
    if (import_event_fn_) {
      import_event_fn_(std::move(event), event_id);
    }
  }

  void set_display_color_conversion_fn(SetDisplayColorConversionFn fn) {
    set_display_color_conversion_fn_ = fn;
  }

  void SetDisplayColorConversion(uint64_t display_id, std::array<float, 3> preoffsets,
                                 std::array<float, 9> coefficients,
                                 std::array<float, 3> postoffsets) override {
    if (set_display_color_conversion_fn_) {
      set_display_color_conversion_fn_(display_id, preoffsets, coefficients, postoffsets);
    }
  }

  void set_check_config_fn(CheckConfigFn fn) { check_config_fn_ = fn; }

  void CheckConfig(bool discard, CheckConfigCallback callback) override {
    fuchsia::hardware::display::ConfigResult result = fuchsia::hardware::display::ConfigResult::OK;
    std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;

    if (check_config_fn_) {
      check_config_fn_(discard, &result, &ops);
    }
    callback(std::move(result), std::move(ops));
  }

  EventSender_& events() { return binding_.events(); }

  void ResetDeviceChannel() { device_channel_.reset(); }

  void ResetControllerBinding() { binding_.Close(ZX_ERR_INTERNAL); }

  fidl::Binding<fuchsia::hardware::display::Controller>& binding() { return binding_; }

 private:
  CheckConfigFn check_config_fn_;
  SetDisplayColorConversionFn set_display_color_conversion_fn_;
  ImportEventFn import_event_fn_;

  fidl::Binding<fuchsia::hardware::display::Controller> binding_;
  zx::channel device_channel_;
};

}  // namespace test
}  // namespace display
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_TESTS_MOCK_DISPLAY_CONTROLLER_H_
