// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "manager_stream_provider.h"

#include <lib/fzl/vmo-mapper.h>

#include <fbl/unique_fd.h>
#include <src/lib/syslog/cpp/logger.h>

static constexpr uint32_t kDevicesPopulated = ZX_USER_SIGNAL_0;
static constexpr uint32_t kStreamCreated = ZX_USER_SIGNAL_1;
static constexpr uint32_t kBuffersAllocated = ZX_USER_SIGNAL_2;

ManagerStreamProvider::~ManagerStreamProvider() {
  if (buffer_collection_) {
    zx_status_t status = buffer_collection_->Close();
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status);
    }
  }
}

std::unique_ptr<StreamProvider> ManagerStreamProvider::Create() {
  auto provider = std::make_unique<ManagerStreamProvider>();
  auto context = sys::ComponentContext::Create();

  // Connect to sysmem.
  zx_status_t status = context->svc()->Connect(provider->allocator_.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to connect to sysmem allocator service";
    return nullptr;
  }

  // Connect to manager.
  status = context->svc()->Connect(provider->manager_.NewRequest(provider->loop_.dispatcher()));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to connect to manager service";
    return nullptr;
  }
  provider->manager_.set_error_handler([provider = provider.get()](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Manager disconnected";
  });

  // Bind event handlers.
  provider->manager_.events().OnDeviceAvailable =
      fit::bind_member(provider.get(), &ManagerStreamProvider::OnDeviceAvailable);
  provider->manager_.events().OnDeviceUnavailable =
      fit::bind_member(provider.get(), &ManagerStreamProvider::OnDeviceUnavailable);
  provider->manager_.events().OnDeviceMuteChanged =
      fit::bind_member(provider.get(), &ManagerStreamProvider::OnDeviceMuteChanged);

  // Create an event to track async events.
  status = zx::event::create(0, &provider->async_events_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return nullptr;
  }

  // Start a thread to process manager and sysmem events.
  status = provider->loop_.StartThread();
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return nullptr;
  }

  // Wait for the devices-populated signal.
  constexpr uint32_t kWaitTimeForPopulateMsec = 500;
  status = provider->async_events_.wait_one(
      kDevicesPopulated, zx::deadline_after(zx::msec(kWaitTimeForPopulateMsec)), nullptr);
  if (status == ZX_ERR_TIMED_OUT) {
    FX_PLOGS(ERROR, status) << "Manager failed to populate known devices within "
                            << kWaitTimeForPopulateMsec << "ms";
    // TODO(39922): remove camera manager workarounds
    FX_LOGS(WARNING) << "Not yet implemented by camera manger - continuing anyway with device id 0";
    // return nullptr;
  } else if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return nullptr;
  }

  FX_LOGS(INFO) << provider->devices_.size() << " devices reported:";
  for (const auto& device : provider->devices_) {
    FX_LOGS(INFO) << "  " << device.first << ": " << device.second.vendor_name() << " | "
                  << device.second.product_name();
  }

  return std::move(provider);
}

// Offer a stream as served through the tester interface.
fit::result<
    std::tuple<fuchsia::sysmem::ImageFormat_2, fuchsia::sysmem::BufferCollectionInfo_2, bool>,
    zx_status_t>
ManagerStreamProvider::ConnectToStream(fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
                                       uint32_t index) {
  if (index > 0) {
    return fit::error(ZX_ERR_OUT_OF_RANGE);
  }

  constexpr uint32_t kDeviceId = 0;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr token;
  zx_status_t status = allocator_->AllocateSharedCollection(token.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> manager_token;
  status = token->Duplicate(ZX_RIGHT_SAME_RIGHTS, manager_token.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  fuchsia::sysmem::BufferCollectionPtr collection;
  status =
      allocator_->BindSharedCollection(std::move(token), collection.NewRequest(loop_.dispatcher()));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  constexpr uint32_t kCampingBufferCount = 2;
  fuchsia::sysmem::BufferCollectionConstraints sysmem_constraints;
  sysmem_constraints.min_buffer_count_for_camping = kCampingBufferCount;
  sysmem_constraints.image_format_constraints_count = 0;
  sysmem_constraints.usage.cpu = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageWrite;
  collection->SetConstraints(true, std::move(sysmem_constraints));

  fuchsia::camera2::StreamConstraints stream_constraints;  // Intentionally empty.
  fuchsia::sysmem::ImageFormat_2 format_ret;
  manager_->ConnectToStream(kDeviceId, std::move(stream_constraints), std::move(manager_token),
                            std::move(request),
                            [this, &format_ret](fuchsia::sysmem::ImageFormat_2 format) {
                              format_ret = std::move(format);
                              async_events_.signal(0, kStreamCreated);
                            });

  fuchsia::sysmem::BufferCollectionInfo_2 buffers_ret;
  collection->WaitForBuffersAllocated(
      [this, &buffers_ret, &collection](zx_status_t status,
                                        fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status);
          return;
        }
        buffers_ret = std::move(buffers);
        collection->Close();
        async_events_.signal(0, kBuffersAllocated);
      });

  constexpr uint32_t kWaitTimeForFormatMsec = 20000;
  status = async_events_.wait_one(kStreamCreated,
                                  zx::deadline_after(zx::msec(kWaitTimeForFormatMsec)), nullptr);
  if (status == ZX_ERR_TIMED_OUT) {
    FX_PLOGS(ERROR, status) << "Manager failed to create the stream within "
                            << kWaitTimeForFormatMsec << "ms";
    return fit::error(status);
  } else if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  constexpr uint32_t kWaitTimeForSysmemMsec = 20000;
  status = async_events_.wait_one(kBuffersAllocated,
                                  zx::deadline_after(zx::msec(kWaitTimeForSysmemMsec)), nullptr);
  if (status == ZX_ERR_TIMED_OUT) {
    FX_PLOGS(ERROR, status) << "Sysmem failed to allocate buffers within " << kWaitTimeForSysmemMsec
                            << "ms";
    return fit::error(status);
  } else if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  // The stream from controller is currently unrotated.
  // TODO: once GDC is hooked up to do the rotation within the controller, set this to 'false'
  return fit::ok(std::make_tuple(std::move(format_ret), std::move(buffers_ret), true));
}

void ManagerStreamProvider::OnDeviceAvailable(int device_id,
                                              fuchsia::camera2::DeviceInfo description,
                                              bool last_known_camera) {
  auto it = devices_.find(device_id);
  if (it != devices_.end()) {
    FX_LOGS(ERROR) << "device id " << device_id << " already reported to us!";
    return;
  }
  devices_.emplace(std::make_pair(device_id, std::move(description)));

  if (last_known_camera) {
    zx_status_t status = async_events_.signal(0, kDevicesPopulated);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status);
      return;
    }
  }

  manager_->AcknowledgeDeviceEvent();
}

void ManagerStreamProvider::OnDeviceUnavailable(int device_id) {
  // TODO(msandy): devices_ needs a lock once manager starts sending its own events
  auto it = devices_.find(device_id);
  if (it == devices_.end()) {
    FX_LOGS(ERROR) << "device id " << device_id << " was never reported to us!";
    return;
  }
  devices_.erase(it);
  manager_->AcknowledgeDeviceEvent();
}

void ManagerStreamProvider::OnDeviceMuteChanged(int device_id, bool currently_muted) {
  manager_->AcknowledgeDeviceEvent();
}
