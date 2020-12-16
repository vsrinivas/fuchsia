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

  MOCK_METHOD3(SetLayerColorConfig, void(uint64_t, uint32_t, std::vector<uint8_t>));

  MOCK_METHOD4(SetLayerImage, void(uint64_t, uint64_t, uint64_t, uint64_t));

  MOCK_METHOD0(ApplyConfig, void());

  MOCK_METHOD2(CheckConfig, void(bool, CheckConfigCallback));

  MOCK_METHOD3(ImportBufferCollection,
               void(uint64_t, fidl::InterfaceHandle<class ::fuchsia::sysmem::BufferCollectionToken>,
                    ImportBufferCollectionCallback));

  MOCK_METHOD3(SetBufferCollectionConstraints,
               void(uint64_t, fuchsia::hardware::display::ImageConfig,
                    SetBufferCollectionConstraintsCallback));

  MOCK_METHOD1(ReleaseBufferCollection, void(uint64_t));

  MOCK_METHOD4(ImportImage, void(fuchsia::hardware::display::ImageConfig, uint64_t, uint32_t,
                                 ImportImageCallback));

  MOCK_METHOD1(ReleaseImage, void(uint64_t));

  MOCK_METHOD4(SetLayerPrimaryPosition,
               void(uint64_t, fuchsia::hardware::display::Transform,
                    fuchsia::hardware::display::Frame, fuchsia::hardware::display::Frame));

  MOCK_METHOD1(CreateLayer, void(CreateLayerCallback));

  MOCK_METHOD1(DestroyLayer, void(uint64_t));

  MOCK_METHOD2(SetDisplayLayers, void(uint64_t, ::std::vector<uint64_t>));

 private:
  void NotImplemented_(const std::string& name) final {}

  fidl::Binding<fuchsia::hardware::display::Controller> binding_;
  zx::channel device_channel_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_MOCK_DISPLAY_CONTROLLER_H_
