// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/camera_manager/camera_manager_impl.h"

#include <fcntl.h>

#include <string>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <fbl/unique_fd.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

static const char* kCameraDevicePath = "/dev/class/camera";

CameraManagerImpl::CameraManagerImpl(async::Loop* loop) {
  // Step #1: Begin monitoring for plug/unplug events for pluggable cameras
  bool idle_seen = false;
  device_watcher_ = fsl::DeviceWatcher::CreateWithIdleCallback(
      kCameraDevicePath,
      // Function to be called when adding a camera:
      fbl::BindMember(this, &CameraManagerImpl::OnDeviceFound),
      // Function to be called when all existing devices have been found:
      // This function captures the local variable idle_seen
      // because it will only be called while we are in this constructor.
      [&idle_seen] { idle_seen = true; });
  if (device_watcher_ == nullptr) {
    FX_LOGS(ERROR) << " failed to create DeviceWatcher.";
    return;
  }
  // Now we need to wait until:
  // #1 we have received the IDLE notification, indicating that all the devices
  // that were plugged in have been added, and
  // #2 all of those devices have finished gathering information from their
  // drivers.
  // We do this by spinning the async loop one event at a time until the devices
  // all report in.  VideoDeviceClients are guaranteed to either gather all the
  // necessary information, or time out (or error out), so we know that this
  // loop will finish.
  zx_status_t status;
  while (!idle_seen || !inactive_devices_.empty()) {
    status = loop->Run(zx::time::infinite(), true);
    if (status != ZX_OK) {
      return;
    }
  }

  // Now bind our context, and publish the public service:
  // Note that Create must be called after we do our looping
  // above, or the request for the CameraManager may fail.
  // TODO(CAM-18):  Change the interface to encourage dynamic detections by the
  // application, and we won't have to do this.
  context_ = sys::ComponentContext::Create();
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

// The dispatcher loop should be shut down when this destructor is called.
// No further messages should be handled after this destructor is called.
CameraManagerImpl::~CameraManagerImpl() {
  // Stop monitoring plug/unplug events.  We are shutting down and
  // no longer care about devices coming and going.
  device_watcher_ = nullptr;
  // In case we just discovered any new devices:
  inactive_devices_.clear();
  // Shut down each currently active device in the system.
  active_devices_.clear();
}

void CameraManagerImpl::OnDeviceFound(int dir_fd, const std::string& filename) {
  auto device = VideoDeviceClient::Create(dir_fd, filename);
  if (!device) {
    FX_LOGS(ERROR) << "Failed to create device " << filename;
    return;
  }

  AddDevice(std::move(device));
}

void CameraManagerImpl::AddDevice(std::unique_ptr<VideoDeviceClient> video_device) {
  // Initially add the device to the inactive devices until we get all
  // the information we need from it.
  inactive_devices_.push_back(std::move(video_device));
  inactive_devices_.back()->Startup(
      [this, id = inactive_devices_.back()->id()](zx_status_t status) {
        OnDeviceStartupComplete(id, status);
      });
}

void CameraManagerImpl::OnDeviceStartupComplete(uint64_t camera_id, zx_status_t status) {
  for (auto iter = inactive_devices_.begin(); iter != inactive_devices_.end(); iter++) {
    if ((*iter)->id() == camera_id) {
      // Now that we found the device, either put it in the active list,
      // or shut it down, depending on the status.
      if (status == ZX_OK) {
        // We put the newly active device in the front of active_devices_,
        // but it doesn't really matter what order the devices are stored in.
        active_devices_.splice(active_devices_.begin(), inactive_devices_, iter);
      } else {
        inactive_devices_.erase(iter);
      }
      break;
    }
  }
}

void CameraManagerImpl::GetDevices(GetDevicesCallback callback) {
  std::vector<fuchsia::camera::DeviceInfo> device_descriptions;
  for (auto& device : active_devices_) {
    device_descriptions.push_back(device->GetDeviceInfo());
  }
  callback(std::move(device_descriptions));
}

void CameraManagerImpl::GetFormats(uint64_t camera_id, uint32_t index,
                                   GetFormatsCallback callback) {
  const std::vector<fuchsia::camera::VideoFormat>* formats;
  for (auto& device : active_devices_) {
    if (device->id() == camera_id) {
      formats = device->GetFormats();
      break;
    }
  }

  size_t min_index = std::max<size_t>(0, std::min<size_t>(index, formats->size() - 1));
  size_t max_index =
      std::min(min_index + fuchsia::camera::MAX_FORMATS_PER_RESPONSE - 1, formats->size() - 1);

  callback(
      std::vector<fuchsia::camera::VideoFormat>(&(*formats)[min_index], &(*formats)[max_index + 1]),
      formats->size());
}

void CameraManagerImpl::CreateStream(fuchsia::camera::VideoStream request,
                                     fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                                     fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                                     zx::eventpair client_token) {
  for (auto& device : active_devices_) {
    if (device->id() == request.camera_id) {
      device->CreateStream(std::move(buffer_collection), request.format.rate, std::move(stream),
                           std::move(client_token));
      break;
    }
  }
}

}  // namespace camera
