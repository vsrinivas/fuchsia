// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_CONTROLLER_STREAM_PROVIDER_H_
#define SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_CONTROLLER_STREAM_PROVIDER_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "stream_provider.h"

class ControllerStreamProvider : public StreamProvider {
 public:
  ~ControllerStreamProvider();
  static std::unique_ptr<StreamProvider> Create();
  fit::result<
      std::tuple<fuchsia::sysmem::ImageFormat_2, fuchsia::sysmem::BufferCollectionInfo_2, bool>,
      zx_status_t>
  ConnectToStream(fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
                  uint32_t index) override;
  virtual std::string GetName() override { return "fuchsia.camera2.Controller service"; }

 private:
  bool streaming_ = false;
  fuchsia::camera2::hal::ControllerSyncPtr controller_;
  fuchsia::sysmem::AllocatorSyncPtr allocator_;
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_;
  fidl::VectorPtr<fuchsia::camera2::hal::Config> configs_;
};

#endif  // SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_CONTROLLER_STREAM_PROVIDER_H_
