// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller_stream_provider.h"

#include <fcntl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/vmo-mapper.h>

#include <fbl/unique_fd.h>
#include <src/lib/fxl/logging.h>

#include "streamptr_wrapper.h"

static constexpr const char* kDevicePath = "/dev/camera-controller/camera-controller-device";

ControllerStreamProvider::~ControllerStreamProvider() {
  if (controller_ && streaming_) {
    zx_status_t status = controller_->DisableStreaming();
    if (status != ZX_OK) {
      FXL_PLOG(WARNING, status) << "Failed to stop streaming via the controller";
    }
  }
  if (buffer_collection_) {
    zx_status_t status = buffer_collection_->Close();
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status);
    }
  }
}

std::unique_ptr<StreamProvider> ControllerStreamProvider::Create() {
  auto provider = std::make_unique<ControllerStreamProvider>();

  // Connect to sysmem.
  zx_status_t status =
      sys::ComponentContext::Create()->svc()->Connect(provider->allocator_.NewRequest());
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to connect to sysmem allocator service";
    return nullptr;
  }
  if (!provider->allocator_) {
    FXL_LOG(ERROR) << "Failed to connect to sysmem allocator service";
    return nullptr;
  }

  // Connect to the controller device.
  int result = open(kDevicePath, O_RDONLY);
  if (result < 0) {
    FXL_LOG(ERROR) << "Error opening " << kDevicePath;
    return nullptr;
  }
  fbl::unique_fd controller_fd(result);
  zx::channel channel;
  status = fdio_get_service_handle(controller_fd.get(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to get service handle";
    return nullptr;
  }
  fuchsia::hardware::camera::DevicePtr device;
  device.Bind(std::move(channel));

  // Connect to the controller interface.
  device->GetChannel2(provider->controller_.NewRequest().TakeChannel());
  if (!provider->controller_) {
    FXL_LOG(ERROR) << "Failed to get controller interface from device";
    return nullptr;
  }

  // Immediately enable streaming.
  status = provider->controller_->EnableStreaming();
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to start streaming via the controller";
  }
  provider->streaming_ = true;

  return std::move(provider);
}

// Offer a stream as served through the tester interface.
std::unique_ptr<fuchsia::camera2::Stream> ControllerStreamProvider::ConnectToStream(
    fuchsia::camera2::Stream::EventSender_* event_handler,
    fuchsia::sysmem::ImageFormat_2* format_out,
    fuchsia::sysmem::BufferCollectionInfo_2* buffers_out, bool* should_rotate_out) {
  if (!format_out || !buffers_out || !should_rotate_out) {
    return nullptr;
  }

  static constexpr const uint32_t kConfigIndex = 0;
  static constexpr const uint32_t kStreamConfigIndex = 0;
  static constexpr const uint32_t kImageFormatIndex = 0;

  if (buffer_collection_.is_bound()) {
    FXL_PLOG(ERROR, ZX_ERR_ALREADY_BOUND) << "Stream already bound by caller.";
    return nullptr;
  }

  // Get the list of valid configs as reported by the controller.
  fidl::VectorPtr<fuchsia::camera2::hal::Config> configs;
  zx_status_t status_return = ZX_OK;
  zx_status_t status = controller_->GetConfigs(&configs, &status_return);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to call GetConfigs";
    return nullptr;
  }
  if (status_return != ZX_OK) {
    FXL_PLOG(ERROR, status_return) << "Failed to get configs";
    return nullptr;
  }
  if (configs->size() <= kConfigIndex) {
    FXL_LOG(ERROR) << "Invalid config index " << kConfigIndex;
    return nullptr;
  }
  auto& config = configs->at(kConfigIndex);
  if (config.stream_configs.size() <= kStreamConfigIndex) {
    FXL_LOG(ERROR) << "Invalid stream config index " << kStreamConfigIndex;
    return nullptr;
  }
  auto& stream_config = config.stream_configs[kStreamConfigIndex];
  if (stream_config.image_formats.size() <= kImageFormatIndex) {
    FXL_LOG(ERROR) << "Invalid image format index " << kImageFormatIndex;
    return nullptr;
  }
  auto& image_format = stream_config.image_formats[kImageFormatIndex];

  // Attempt to create a buffer collection using controller-provided constraints.
  if (!allocator_) {
    FXL_LOG(ERROR) << "Allocator is dead!";
  }
  status = allocator_->AllocateNonSharedCollection(buffer_collection_.NewRequest());
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to allocate new collection";
    return nullptr;
  }
  status = buffer_collection_->SetConstraints(true, stream_config.constraints);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to set constraints to those reported by the controller";
    return nullptr;
  }
  status_return = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  status = buffer_collection_->WaitForBuffersAllocated(&status_return, &buffers);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to call WaitForBuffersAllocated";
    return nullptr;
  }
  if (status_return != ZX_OK) {
    FXL_PLOG(ERROR, status_return) << "Failed to allocate buffers";
    return nullptr;
  }

  // TODO(fxb/37296): remove ISP workarounds
  // The ISP does not currently write the chroma layer, so initialize all VMOs to 128 (grayscale).
  // This avoids the resulting image from appearing as 100% saturated green.
  for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
    fzl::VmoMapper mapper;
    status = mapper.Map(buffers.buffers[i].vmo, 0, buffers.settings.buffer_settings.size_bytes,
                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Error mapping vmo";
      return nullptr;
    }
    memset(mapper.start(), 128, mapper.size());
    mapper.Unmap();
  }

  // Duplicate the collection info so it can be returned to the caller.
  fuchsia::sysmem::BufferCollectionInfo_2 buffers_for_caller;
  status = buffers.Clone(&buffers_for_caller);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to clone buffer collection";
    return nullptr;
  }

  // Create the stream using the created buffer collection.
  fuchsia::camera2::StreamPtr stream;
  stream.set_error_handler(
      [](zx_status_t status) { FXL_PLOG(ERROR, status) << "Server disconnected"; });
  stream.events().OnFrameAvailable =
      fit::bind_member(event_handler, &fuchsia::camera2::Stream::EventSender_::OnFrameAvailable);
  status = controller_->CreateStream(kConfigIndex, kStreamConfigIndex, kImageFormatIndex,
                                     std::move(buffers), stream.NewRequest());
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to create stream";
    return nullptr;
  }

  // The stream from controller is currently unrotated.
  // TODO: once GDC is hooked up to do the rotation within the controller, set this to 'false'
  *should_rotate_out = true;

  *format_out = std::move(image_format);
  *buffers_out = std::move(buffers_for_caller);
  return std::make_unique<StreamPtrWrapper>(std::move(stream));
}
