// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_TESTS_MOCK_DISPLAY_CONTROLLER_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_TESTS_MOCK_DISPLAY_CONTROLLER_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/handles/object_info.h"
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
  using SetMinimumRgbFn = std::function<void(uint8_t)>;
  using ImportEventFn = std::function<void(zx::event event, uint64_t event_id)>;
  using AcknowledgeVsyncFn = std::function<void(uint64_t cookie)>;
  using SetDisplayLayersFn = std::function<void(uint64_t, std::vector<uint64_t>)>;
  using SetLayerPrimaryPositionFn =
      std::function<void(uint64_t, fuchsia::hardware::display::Transform,
                         fuchsia::hardware::display::Frame, fuchsia::hardware::display::Frame)>;

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
    set_display_color_conversion_fn_ = std::move(fn);
  }

  void set_minimum_rgb_fn(SetMinimumRgbFn fn) { set_minimum_rgb_fn_ = std::move(fn); }

  void set_set_display_layers_fn(SetDisplayLayersFn fn) { set_display_layers_fn_ = std::move(fn); }

  void set_layer_primary_position_fn(SetLayerPrimaryPositionFn fn) {
    set_layer_primary_position_fn_ = std::move(fn);
  }

  void SetDisplayColorConversion(uint64_t display_id, std::array<float, 3> preoffsets,
                                 std::array<float, 9> coefficients,
                                 std::array<float, 3> postoffsets) override {
    if (set_display_color_conversion_fn_) {
      set_display_color_conversion_fn_(display_id, preoffsets, coefficients, postoffsets);
    }
  }

  void SetMinimumRgb(uint8_t minimum, SetMinimumRgbCallback callback) override {
    auto result = fuchsia::hardware::display::Controller_SetMinimumRgb_Result::WithResponse(
        fuchsia::hardware::display::Controller_SetMinimumRgb_Response());
    if (set_minimum_rgb_fn_) {
      set_minimum_rgb_fn_(minimum);
    }

    callback(std::move(result));
  }

  void CreateLayer(CreateLayerCallback callback) override {
    static uint64_t layer_id = 1;
    callback(ZX_OK, layer_id++);
  }

  void SetDisplayLayers(uint64_t display_id, ::std::vector<uint64_t> layer_ids) override {
    if (set_display_layers_fn_) {
      set_display_layers_fn_(display_id, layer_ids);
    }
  }

  void ImportImage(fuchsia::hardware::display::ImageConfig image_config, uint64_t collection_id,
                   uint32_t index, ImportImageCallback callback) override {
    static uint64_t image_id = 1;
    callback(ZX_OK, image_id++);
  }

  void SetLayerPrimaryPosition(uint64_t layer_id, fuchsia::hardware::display::Transform transform,
                               fuchsia::hardware::display::Frame src_frame,
                               fuchsia::hardware::display::Frame dest_frame) override {
    if (set_layer_primary_position_fn_) {
      set_layer_primary_position_fn_(layer_id, transform, src_frame, dest_frame);
    }
  }

  void set_check_config_fn(CheckConfigFn fn) { check_config_fn_ = std::move(fn); }

  void CheckConfig(bool discard, CheckConfigCallback callback) override {
    fuchsia::hardware::display::ConfigResult result = fuchsia::hardware::display::ConfigResult::OK;
    std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
    if (check_config_fn_) {
      check_config_fn_(discard, &result, &ops);
    }

    callback(std::move(result), std::move(ops));
  }

  void set_acknowledge_vsync_fn(AcknowledgeVsyncFn acknowledge_vsync_fn) {
    acknowledge_vsync_fn_ = std::move(acknowledge_vsync_fn);
  }

  void AcknowledgeVsync(uint64_t cookie) override {
    if (acknowledge_vsync_fn_) {
      acknowledge_vsync_fn_(cookie);
    }
  }

  EventSender_& events() { return binding_.events(); }

  void ResetDeviceChannel() { device_channel_.reset(); }

  void ResetControllerBinding() { binding_.Close(ZX_ERR_INTERNAL); }

  fidl::Binding<fuchsia::hardware::display::Controller>& binding() { return binding_; }

 private:
  CheckConfigFn check_config_fn_;
  SetDisplayColorConversionFn set_display_color_conversion_fn_;
  SetMinimumRgbFn set_minimum_rgb_fn_;
  ImportEventFn import_event_fn_;
  AcknowledgeVsyncFn acknowledge_vsync_fn_;
  SetDisplayLayersFn set_display_layers_fn_;
  SetLayerPrimaryPositionFn set_layer_primary_position_fn_;

  fidl::Binding<fuchsia::hardware::display::Controller> binding_;
  zx::channel device_channel_;
};

}  // namespace test
}  // namespace display
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_TESTS_MOCK_DISPLAY_CONTROLLER_H_
