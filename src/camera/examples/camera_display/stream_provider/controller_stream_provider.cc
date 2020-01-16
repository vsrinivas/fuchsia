// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller_stream_provider.h"

#include <fcntl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/errors.h>

#include <fbl/unique_fd.h>

#include "src/lib/syslog/cpp/logger.h"

static constexpr const char* kDevicePath = "/dev/camera-controller/camera-controller-device";
// NOTE: These indexes are coming from Sherlock controller configuration.
// See sherlock-configs.cc for reference.
static constexpr uint32_t kMonitorConfig = 1;
static constexpr uint32_t kMlFrStream = 0;
static constexpr uint32_t kMlDsStream = 1;
static constexpr uint32_t kMonitoringStream = 2;

ControllerStreamProvider::~ControllerStreamProvider() {
  for (auto& buffer_collection : buffer_collections_) {
    if (buffer_collection.second) {
      zx_status_t status = buffer_collection.second->Close();
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status);
      }
    }
  }
}

std::unique_ptr<StreamProvider> ControllerStreamProvider::Create() {
  auto provider = std::make_unique<ControllerStreamProvider>();

  // Connect to sysmem.
  zx_status_t status =
      sys::ComponentContext::Create()->svc()->Connect(provider->allocator_.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to connect to sysmem allocator service";
    return nullptr;
  }
  if (!provider->allocator_) {
    FX_LOGS(ERROR) << "Failed to connect to sysmem allocator service";
    return nullptr;
  }

  // Connect to the controller device.
  int result = open(kDevicePath, O_RDONLY);
  if (result < 0) {
    FX_LOGS(ERROR) << "Error opening " << kDevicePath;
    return nullptr;
  }
  fbl::unique_fd controller_fd(result);
  zx::channel channel;
  status = fdio_get_service_handle(controller_fd.get(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get service handle";
    return nullptr;
  }
  fuchsia::hardware::camera::DevicePtr device;
  device.Bind(std::move(channel));

  // Connect to the controller interface.
  device->GetChannel2(provider->controller_.NewRequest().TakeChannel());
  if (!provider->controller_) {
    FX_LOGS(ERROR) << "Failed to get controller interface from device";
    return nullptr;
  }

  // Get the list of valid configs as reported by the controller.
  zx_status_t status_return = ZX_OK;
  status = provider->controller_->GetConfigs(&provider->configs_, &status_return);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to call GetConfigs";
    return nullptr;
  }
  if (status_return != ZX_OK) {
    FX_PLOGS(ERROR, status_return) << "Failed to get configs";
    return nullptr;
  }

  return std::move(provider);
}

// Offer a stream as served through the controller service provided by the driver.
fit::result<
    std::tuple<fuchsia::sysmem::ImageFormat_2, fuchsia::sysmem::BufferCollectionInfo_2, bool>,
    zx_status_t>
ControllerStreamProvider::ConnectToStream(fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
                                          uint32_t index) {
  uint32_t config_index = 0;
  uint32_t stream_config_index = 0;
  uint32_t image_format_index = 0;
  // Displaying all sub-streams from the monitoring config side-by-side;
  // 0: full-res machine learning stream;
  // 1: downscaled machine learning stream;
  // 2: monitoring stream;
  switch (index) {
    case 0:
      config_index = kMonitorConfig;
      stream_config_index = kMlFrStream;
      image_format_index = 0;
      break;
    case 1:
      config_index = kMonitorConfig;
      stream_config_index = kMlDsStream;
      image_format_index = 0;
      break;
    case 2:
      config_index = kMonitorConfig;
      stream_config_index = kMonitoringStream;
      image_format_index = 0;
      break;
    default:
      return fit::error(ZX_ERR_OUT_OF_RANGE);
  }

  auto it = buffer_collections_.find(index);
  if (it == buffer_collections_.end()) {
    buffer_collections_.emplace(index, nullptr);
  }
  if (buffer_collections_[index].is_bound()) {
    FX_PLOGS(ERROR, ZX_ERR_ALREADY_BOUND) << "Stream already bound by caller.";
    return fit::error(ZX_ERR_ALREADY_BOUND);
  }

  if (config_index >= configs_->size()) {
    FX_LOGS(ERROR) << "Invalid config index " << config_index;
    return fit::error(ZX_ERR_BAD_STATE);
  }
  auto& config = configs_->at(config_index);
  if (stream_config_index >= config.stream_configs.size()) {
    FX_LOGS(ERROR) << "Invalid stream config index " << stream_config_index;
    return fit::error(ZX_ERR_BAD_STATE);
  }
  auto& stream_config = config.stream_configs[stream_config_index];
  if (image_format_index >= stream_config.image_formats.size()) {
    FX_LOGS(ERROR) << "Invalid image format index " << image_format_index;
    return fit::error(ZX_ERR_BAD_STATE);
  }
  auto& image_format = stream_config.image_formats[image_format_index];

  // Attempt to create a buffer collection using controller-provided constraints.
  if (!allocator_) {
    FX_LOGS(ERROR) << "Allocator is dead!";
  }
  zx_status_t status =
      allocator_->AllocateNonSharedCollection(buffer_collections_[index].NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to allocate new collection";
    return fit::error(status);
  }
  status = buffer_collections_[index]->SetConstraints(true, stream_config.constraints);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to set constraints to those reported by the controller";
    return fit::error(status);
  }
  zx_status_t status_return = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  status = buffer_collections_[index]->WaitForBuffersAllocated(&status_return, &buffers);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to call WaitForBuffersAllocated";
    return fit::error(status);
  }
  if (status_return != ZX_OK) {
    FX_PLOGS(ERROR, status_return) << "Failed to allocate buffers";
    return fit::error(status_return);
  }

  // TODO(fxb/37296): remove ISP workarounds
  // The ISP does not currently write the chroma layer, so initialize all VMOs to 128 (grayscale).
  // This avoids the resulting image from appearing as 100% saturated green.
  for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
    fzl::VmoMapper mapper;
    status = mapper.Map(buffers.buffers[i].vmo, 0, buffers.settings.buffer_settings.size_bytes,
                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Error mapping vmo";
      return fit::error(status);
    }
    memset(mapper.start(), 128, mapper.size());
    mapper.Unmap();
  }

  // Duplicate the collection info so it can be returned to the caller.
  fuchsia::sysmem::BufferCollectionInfo_2 buffers_for_caller;
  status = buffers.Clone(&buffers_for_caller);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to clone buffer collection";
    return fit::error(status);
  }

  // Create the stream using the created buffer collection.
  status = controller_->CreateStream(config_index, stream_config_index, image_format_index,
                                     std::move(buffers), std::move(request));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create stream";
    return fit::error(status);
  }

  return fit::ok(std::make_tuple(std::move(image_format), std::move(buffers_for_caller), false));
}
