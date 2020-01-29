// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/device_impl.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "lib/async/default.h"
#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/util.h"

DeviceImpl::DeviceImpl() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

DeviceImpl::~DeviceImpl() {
  Unbind(controller_);
  loop_.Quit();
  loop_.JoinThreads();
}

fit::result<std::unique_ptr<DeviceImpl>, zx_status_t> DeviceImpl::Create(
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller,
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator) {
  auto device = std::make_unique<DeviceImpl>();

  ZX_ASSERT(zx::event::create(0, &device->bad_state_event_) == ZX_OK);

  ZX_ASSERT(device->allocator_.Bind(std::move(allocator), device->loop_.dispatcher()) == ZX_OK);

  constexpr auto kControllerDisconnected = ZX_USER_SIGNAL_0;
  constexpr auto kGetDeviceInfoReturned = ZX_USER_SIGNAL_1;
  constexpr auto kGetConfigsReturned = ZX_USER_SIGNAL_2;
  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);

  // Bind the controller interface and get some initial startup information.

  ZX_ASSERT(device->controller_.Bind(std::move(controller), device->loop_.dispatcher()) == ZX_OK);

  zx_status_t controller_status = ZX_OK;
  device->controller_.set_error_handler([&](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Controller server disconnected during initialization.";
    controller_status = status;
    ZX_ASSERT(event.signal(0, kControllerDisconnected) == ZX_OK);
  });

  device->controller_->GetDeviceInfo(
      [&, device = device.get()](fuchsia::camera2::DeviceInfo device_info) {
        device->device_info_ = std::move(device_info);
        ZX_ASSERT(event.signal(0, kGetDeviceInfoReturned) == ZX_OK);
      });

  zx_status_t get_configs_status = ZX_OK;
  device->controller_->GetConfigs([&, device = device.get()](
                                      fidl::VectorPtr<fuchsia::camera2::hal::Config> configs,
                                      zx_status_t status) {
    get_configs_status = status;
    if (status == ZX_OK) {
      if (configs.has_value() && !configs.value().empty()) {
        device->configs_ = std::move(configs.value());
        for (const auto& config : device->configs_) {
          auto result = Convert(config);
          if (result.is_error()) {
            get_configs_status = result.error();
            FX_PLOGS(ERROR, get_configs_status);
            break;
          }
          device->configurations_.push_back(result.take_value());
        }
      } else {
        get_configs_status = ZX_ERR_INTERNAL;
        FX_PLOGS(ERROR, get_configs_status) << "Controller returned null or empty configs list.";
      }
    }
    device->SetConfiguration(0);
    ZX_ASSERT(event.signal(0, kGetConfigsReturned) == ZX_OK);
  });

  // Start the device thread and begin processing messages.

  ZX_ASSERT(device->loop_.StartThread("Camera Device Thread") == ZX_OK);

  // Wait for either an error, or for all expected callbacks to occur.

  zx_signals_t signaled{};
  ZX_ASSERT(WaitMixed(event, kGetDeviceInfoReturned | kGetConfigsReturned, kControllerDisconnected,
                      zx::time::infinite(), &signaled) == ZX_OK);
  if (signaled & kControllerDisconnected) {
    FX_PLOGS(ERROR, controller_status);
    return fit::error(controller_status);
  }

  // Rebind the controller error handler.

  ZX_ASSERT(async::PostTask(device->loop_.dispatcher(), [device = device.get()]() {
              device->controller_.set_error_handler(
                  fit::bind_member(device, &DeviceImpl::OnControllerDisconnected));
            }) == ZX_OK);

  return fit::ok(std::move(device));
}

fidl::InterfaceRequestHandler<fuchsia::camera3::Device> DeviceImpl::GetHandler() {
  return fit::bind_member(this, &DeviceImpl::OnNewRequest);
}

zx::event DeviceImpl::GetBadStateEvent() {
  zx::event event;
  ZX_ASSERT(bad_state_event_.duplicate(ZX_RIGHTS_BASIC, &event) == ZX_OK);
  return event;
}

void DeviceImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  auto task = [this, request = std::move(request)]() mutable {
    if (!clients_.empty()) {
      FX_PLOGS(INFO, ZX_ERR_ALREADY_BOUND) << Messages::kDeviceAlreadyBound;
      request.Close(ZX_ERR_ALREADY_BOUND);
      return;
    }
    auto client = std::make_unique<Client>(*this, client_id_next_, std::move(request));
    clients_.emplace(client_id_next_++, std::move(client));
  };
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), std::move(task)) == ZX_OK);
}

void DeviceImpl::OnControllerDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Controller disconnected unexpectedly.";
  ZX_ASSERT(bad_state_event_.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);
}

void DeviceImpl::PostRemoveClient(uint64_t id) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, id]() { clients_.erase(id); }) == ZX_OK);
}

void DeviceImpl::PostSetConfiguration(uint32_t index) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, index]() { SetConfiguration(index); }) ==
            ZX_OK);
}

void DeviceImpl::SetConfiguration(uint32_t index) {
  streams_.clear();
  streams_.resize(configurations_[index].streams.size());
  current_configuration_index_ = index;
}

void DeviceImpl::PostConnectToStream(
    uint32_t index, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, index, token = std::move(token),
                                                 request = std::move(request)]() mutable {
              ConnectToStream(index, std::move(token), std::move(request));
            }) == ZX_OK);
}

void DeviceImpl::ConnectToStream(
    uint32_t index, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  if (index > streams_.size()) {
    FX_PLOGS(INFO, ZX_ERR_INVALID_ARGS) << "Client requested invalid stream index " << index;
    request.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (streams_[index]) {
    FX_PLOGS(INFO, ZX_ERR_ALREADY_BOUND) << Messages::kStreamAlreadyBound;
    request.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }

  // Negotiate buffers for this stream.
  // TODO(44770): Watch for buffer collection events.
  fuchsia::sysmem::BufferCollectionPtr collection;
  allocator_->BindSharedCollection(std::move(token), collection.NewRequest(loop_.dispatcher()));
  collection->SetConstraints(
      true, configs_[current_configuration_index_].stream_configs[index].constraints);
  collection->WaitForBuffersAllocated(
      [this, index, request = std::move(request), collection = std::move(collection)](
          zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) mutable {
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Failed to allocate buffers for stream.";
          request.Close(status);
          return;
        }

        // Assign friendly names to each buffer for debugging and profiling.
        for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
          std::ostringstream oss;
          oss << "Camera Config " << current_configuration_index_ << " Stream " << index
              << " Buffer " << i;
          auto str = oss.str();
          ZX_ASSERT(buffers.buffers[i].vmo.set_property(ZX_PROP_NAME, str.c_str(), str.length()) ==
                    ZX_OK);
        }

        // Get the legacy stream using the negotiated buffers.
        fidl::InterfaceHandle<fuchsia::camera2::Stream> legacy_stream;
        controller_->CreateStream(current_configuration_index_, index, 0, std::move(buffers),
                                  legacy_stream.NewRequest());

        // Create the stream. When the last client disconnects, post a task to the device thread to
        // destroy the stream.
        auto task = [this, index]() {
          ZX_ASSERT(async::PostTask(loop_.dispatcher(),
                                    [this, index]() { streams_[index] = nullptr; }) == ZX_OK);
        };
        streams_[index] = std::make_unique<StreamImpl>(std::move(legacy_stream), std::move(request),
                                                       std::move(task));
        collection->Close();
      });
}
