// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_MOCK_DISPLAY_CONTROLLER_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_MOCK_DISPLAY_CONTROLLER_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fsl/handles/object_info.h>

#include <src/ui/scenic/lib/gfx/tests/gfx_test.h>

namespace scenic_impl {
namespace gfx {
namespace test {

class MockDisplayController : public fuchsia::hardware::display::testing::Controller_TestBase {
 public:

    MockDisplayController() : binding_(this) {}

  void NotImplemented_(const std::string& name) final {}

  void ImportEvent(zx::event event, uint64_t event_id) override {
    last_imported_event_koid_ = fsl::GetKoid(event.get());
    last_imported_event_id_ = event_id;
  }

  void SetDisplayColorConversion(uint64_t display_id, std::array<float, 3> preoffsets,
                                 std::array<float, 9> coefficients,
                                 std::array<float, 3> postoffsets) override {
    color_conversion_display_id_ = display_id;
    color_conversion_preoffsets_ = preoffsets;
    color_conversion_coefficients_ = coefficients;
    color_conversion_postoffsets_ = postoffsets;
  }

  void WaitForMessage() {
      binding_.WaitForMessage();
  }

  void Bind(zx::channel device_channel, zx::channel controller_channel,
            async_dispatcher_t* dispatcher = nullptr) {
    device_channel_ = std::move(device_channel);
    binding_.Bind(fidl::InterfaceRequest<fuchsia::hardware::display::Controller>(
                      std::move(controller_channel)),
                  dispatcher);
  }


  using CheckConfigFn =
      std::function<void(bool, fuchsia::hardware::display::ConfigResult*,
                          std::vector<fuchsia::hardware::display::ClientCompositionOp>*)>;

  void set_check_config_fn(CheckConfigFn fn) { check_config_fn_ = fn; }

  void CheckConfig(bool discard, CheckConfigCallback callback) override {
    fuchsia::hardware::display::ConfigResult result = fuchsia::hardware::display::ConfigResult::OK;
    std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;

    if (check_config_fn_) {
      check_config_fn_(discard, &result, &ops);
    }
    callback(std::move(result), std::move(ops));
  }

  zx_koid_t last_imported_event_koid() const { return last_imported_event_koid_; }
  uint64_t last_imported_event_id() const { return last_imported_event_id_; }

  uint64_t color_conversion_display_id() const { return color_conversion_display_id_; }
  std::array<float, 3> color_conversion_preoffsets() const { return color_conversion_preoffsets_; }
  std::array<float, 9> color_conversion_coefficients() const {
    return color_conversion_coefficients_;
  }
  std::array<float, 3> color_conversion_postoffsets() const {
    return color_conversion_postoffsets_;
  }

 private:
  fidl::Binding<fuchsia::hardware::display::Controller> binding_;
  zx::channel device_channel_;


  zx_koid_t last_imported_event_koid_ = 0;
  uint64_t last_imported_event_id_ = fuchsia::hardware::display::invalidId;

  uint64_t color_conversion_display_id_;
  std::array<float, 3> color_conversion_preoffsets_;
  std::array<float, 9> color_conversion_coefficients_;
  std::array<float, 3> color_conversion_postoffsets_;

  CheckConfigFn check_config_fn_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_MOCK_DISPLAY_CONTROLLER_H_
