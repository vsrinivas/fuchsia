// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/stream_provider.h"

#include <fuchsia/camera2/hal/cpp/fidl.h>  // TODO(48124) - camera2 going away
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>  // PostTask
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <src/camera/bin/device/device_impl.h>
#include <src/lib/syslog/cpp/logger.h>

namespace camera {

StreamProvider::StreamProvider(async::Loop* loop) : loop_(loop) {}

StreamProvider::~StreamProvider() {}

std::string StreamProvider::GetName() { return "fuchsia.camera3"; }

zx_status_t StreamProvider::Initialize() {
  uint32_t kDeviceId = 1;  // TODO(48506) - Hard code device
  uint32_t kConfigId = 1;  // TODO(48506) - Hard code config
  uint32_t kStreamId = 2;  // TODO(48506) - Hard code stream

  context_ = sys::ComponentContext::Create();

  // Create an event to track async events
  zx_status_t status = zx::event::create(0, &async_events_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "zx::event::create()";
    return status;
  }

  // Open connection to device watcher
  status = context_->svc()->Connect(watcher_ptr_.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Connect()";
    return status;
  }

  // Fetch list of cameras
  std::vector<fuchsia::camera3::WatchDevicesEvent> events;
  status = watcher_ptr_->WatchDevices(&events);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "WatchDevices()";
    return status;
  }

  for (auto& event : events) {
    if (event.is_added()) {
      cameras_.push_back(event.added());
    }
  }

  // Connect to device
  status = watcher_ptr_->ConnectToDevice(kDeviceId, device_ptr_.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "ConnectToDevice()";
    return status;
  }

  // Connect to allocator service
  status = context_->svc()->Connect(allocator_ptr_.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Connect()";
    return status;
  }

  // Fetch camera configurations
  status = device_ptr_->GetConfigurations(&configurations_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "GetConfigurations()";
    return status;
  }

  // Remember which format was requested
  ZX_ASSERT(configurations_.size() > kConfigId);
  ZX_ASSERT(configurations_[kConfigId].streams.size() > kStreamId);
  format_ = configurations_[kConfigId].streams[kStreamId].image_format;

  // Select the specified stream configuration
  status = device_ptr_->SetCurrentConfiguration(kConfigId);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "SetCurrentConfiguration(" << kConfigId << ")";
    return status;
  }

  // Connect to the specified stream
  status = device_ptr_->ConnectToStream(kStreamId, stream_ptr_.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "ConnectToStream(" << kStreamId << ")";
    return status;
  }

  // Allocate buffer collection
  fuchsia::sysmem::BufferCollectionTokenSyncPtr token_orig;
  status = allocator_ptr_->AllocateSharedCollection(token_orig.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "AllocateSharedCollection()";
    return status;
  }

  status = stream_ptr_->SetBufferCollection(std::move(token_orig));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "SetBufferCollection()";
    return status;
  }

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_back;
  status = stream_ptr_->WatchBufferCollection(&token_back);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "WatchBufferCollection()";
    return status;
  }

  // Bind shared collection
  fuchsia::sysmem::BufferCollectionSyncPtr collection;
  status = allocator_ptr_->BindSharedCollection(std::move(token_back), collection.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "BindSharedCollection()";
    return status;
  }

  // How many buffers are we camping on?
  constexpr uint32_t kCampingBufferCount = 4;  // TODO(48506) - probably too many

  // Set collection constraints
  fuchsia::sysmem::BufferCollectionConstraints sysmem_constraints;
  sysmem_constraints.min_buffer_count_for_camping = kCampingBufferCount;
  sysmem_constraints.image_format_constraints_count = 0;
  sysmem_constraints.usage.cpu = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageWrite;
  status = collection->SetConstraints(true, std::move(sysmem_constraints));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "SetConstraints()";
    return status;
  }

  // Wait for buffers to be allocated
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  status = collection->WaitForBuffersAllocated(&status, &buffers);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "WaitForBuffersAllocated()";
    return status;
  }

  buffer_collection_info_ = std::move(buffers);

  status = collection->Close();
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Close()";
    return status;
  }

  return ZX_OK;
}

fit::result<std::tuple<fuchsia::sysmem::ImageFormat_2, fuchsia::sysmem::BufferCollectionInfo_2>,
            zx_status_t>
StreamProvider::ConnectToStream(FrameCallback frame_callback) {
  frame_callback_ = std::move(frame_callback);
  return fit::ok(std::make_tuple(std::move(format_), std::move(buffer_collection_info_)));
}

zx_status_t StreamProvider::PostGetNextFrame() {
  async::PostTask(loop_->dispatcher(), [this]() { GetNextFrame(); });
  return ZX_OK;
}

zx_status_t StreamProvider::GetNextFrame() {
  fuchsia::camera3::FrameInfo frame_info3;
  stream_ptr_->GetNextFrame(&frame_info3);
  uint32_t buffer_id = frame_info3.buffer_index;
  SaveReleaseInfo(buffer_id, &frame_info3);
  if (frame_callback_) {
    // TODO(48124) - camera2 going away
    fuchsia::camera2::FrameAvailableInfo info2;

    // TODO(48124) - camera2 going away
    info2.frame_status = fuchsia::camera2::FrameStatus::OK;

    info2.buffer_id = buffer_id;
    frame_callback_(std::move(info2));
  } else {
    ReleaseFrame(buffer_id);
  }
  PostGetNextFrame();
  return ZX_OK;
}

zx_status_t StreamProvider::SaveReleaseInfo(uint32_t buffer_id,
                                            fuchsia::camera3::FrameInfo* frame_info3) {
  // TODO(48507) - NOT THREAD SAFE!
  if (release_fences[buffer_id].valid) {
    FX_LOGS(ERROR) << "Buffer ID already valid: " << buffer_id;
  }

  release_fences[buffer_id].valid = false;
  release_fences[buffer_id].release_fence = std::move(frame_info3->release_fence);
  release_fences[buffer_id].valid = true;
  return ZX_OK;
}

zx_status_t StreamProvider::ReleaseFrame(uint32_t buffer_id) {
  // TODO(48507) - NOT THREAD SAFE!
  if (release_fences[buffer_id].valid) {
    zx::eventpair release_fence = std::move(release_fences[buffer_id].release_fence);
    release_fences[buffer_id].valid = false;
    release_fence.reset();
  } else {
    FX_LOGS(ERROR) << "Buffer ID invalid: " << buffer_id;
  }
  return ZX_OK;
}

std::unique_ptr<StreamProvider> StreamProvider::Create(async::Loop* loop) {
  return std::make_unique<StreamProvider>(loop);
}

}  // namespace camera
