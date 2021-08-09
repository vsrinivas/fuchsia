// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_MOCK_DISPLAY_CONTROLLER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_MOCK_DISPLAY_CONTROLLER_H_

#include <fuchsia/hardware/display/cpp/fidl_test_base.h>

namespace flatland {

class MockDisplayController : public fuchsia::hardware::display::testing::Controller_TestBase {
 public:
  explicit MockDisplayController() : binding_(this) {}

  void WaitForMessage() { binding_.WaitForMessage(); }

  void Bind(zx::channel device_channel, zx::channel controller_channel,
            async_dispatcher_t* dispatcher = nullptr) {
    device_channel_ = std::move(device_channel);
    binding_.Bind(fidl::InterfaceRequest<fuchsia::hardware::display::Controller>(
                      std::move(controller_channel)),
                  dispatcher);
  }

  MOCK_METHOD(void, SetLayerColorConfig, (uint64_t, uint32_t, std::vector<uint8_t>));

  MOCK_METHOD(void, SetLayerImage, (uint64_t, uint64_t, uint64_t, uint64_t));

  MOCK_METHOD(void, ApplyConfig, ());

  MOCK_METHOD(void, CheckConfig, (bool, CheckConfigCallback));

  MOCK_METHOD(void, ImportBufferCollection,
              (uint64_t, fidl::InterfaceHandle<class ::fuchsia::sysmem::BufferCollectionToken>,
               ImportBufferCollectionCallback));

  MOCK_METHOD(void, SetBufferCollectionConstraints,
              (uint64_t, fuchsia::hardware::display::ImageConfig,
               SetBufferCollectionConstraintsCallback));

  MOCK_METHOD(void, ReleaseBufferCollection, (uint64_t));

  MOCK_METHOD(void, ImportImage,
              (fuchsia::hardware::display::ImageConfig, uint64_t, uint32_t, ImportImageCallback));

  MOCK_METHOD(void, ReleaseImage, (uint64_t));

  MOCK_METHOD(void, SetLayerPrimaryConfig, (uint64_t, fuchsia::hardware::display::ImageConfig));

  MOCK_METHOD(void, SetLayerPrimaryPosition,
              (uint64_t, fuchsia::hardware::display::Transform, fuchsia::hardware::display::Frame,
               fuchsia::hardware::display::Frame));

  MOCK_METHOD(void, SetLayerPrimaryAlpha, (uint64_t, fuchsia::hardware::display::AlphaMode, float));

  MOCK_METHOD(void, CreateLayer, (CreateLayerCallback));

  MOCK_METHOD(void, DestroyLayer, (uint64_t));

  MOCK_METHOD(void, SetDisplayLayers, (uint64_t, ::std::vector<uint64_t>));

 private:
  void NotImplemented_(const std::string& name) final {}

  fidl::Binding<fuchsia::hardware::display::Controller> binding_;
  zx::channel device_channel_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_MOCK_DISPLAY_CONTROLLER_H_
