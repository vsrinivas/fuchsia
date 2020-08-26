// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/device_impl.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/util.h"
#include "src/lib/fsl/handles/object_info.h"

DeviceImpl::DeviceImpl()
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), button_listener_binding_(this) {}

DeviceImpl::~DeviceImpl() {
  Unbind(controller_);
  Unbind(allocator_);
  Unbind(registry_);
  async::PostTask(loop_.dispatcher(), [this] { loop_.Quit(); });
  loop_.JoinThreads();
}

fit::result<std::unique_ptr<DeviceImpl>, zx_status_t> DeviceImpl::Create(
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller,
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator,
    fuchsia::ui::policy::DeviceListenerRegistryHandle registry) {
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

  zx_status_t get_next_config_status = ZX_OK;
  fit::function<void(std::unique_ptr<fuchsia::camera2::hal::Config>, zx_status_t)>
      get_next_config_callback =
          [&, device = device.get()](std::unique_ptr<fuchsia::camera2::hal::Config> config,
                                     zx_status_t status) {
            get_next_config_status = status;
            if (status == ZX_OK) {
              auto result = Convert(*config);
              if (result.is_error()) {
                get_next_config_status = result.error();
                FX_PLOGS(ERROR, get_next_config_status);
                return;
              }
              device->configurations_.push_back(result.take_value());
              device->configs_.push_back(std::move(*config));

              // Call again to get remaining configs.
              device->controller_->GetNextConfig(get_next_config_callback.share());
              return;
            }
            if (status == ZX_ERR_STOP) {
              get_next_config_status = ZX_OK;
              device->SetConfiguration(0);
            } else {
              get_next_config_status = ZX_ERR_INTERNAL;
              FX_PLOGS(ERROR, status)
                  << "Controller unexpectedly returned error or null/empty configs list.";
            }

            ZX_ASSERT(event.signal(0, kGetConfigsReturned) == ZX_OK);
          };

  device->controller_->GetNextConfig(get_next_config_callback.share());

  // Bind the registry interface and register the device as a listener.

  ZX_ASSERT(device->registry_.Bind(std::move(registry), device->loop_.dispatcher()) == ZX_OK);
  device->registry_->RegisterMediaButtonsListener(
      device->button_listener_binding_.NewBinding(device->loop_.dispatcher()));

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
  PostBind(std::move(request), true);
}

void DeviceImpl::PostBind(fidl::InterfaceRequest<fuchsia::camera3::Device> request,
                          bool exclusive) {
  auto task = [this, request = std::move(request), exclusive]() mutable {
    if (exclusive && !clients_.empty()) {
#if CAMERA_POLICY_ALLOW_REPLACEMENT_CONNECTIONS
      FX_LOGS(WARNING) << "!!!! CAMERA POLICY OVERRIDE FORCING DISCONNECT OF " << clients_.size()
                       << " EXISTING CLIENTS !!!!";
      clients_.clear();
#else
      request.Close(ZX_ERR_ALREADY_BOUND);
      return;
#endif
    }
    auto client = std::make_unique<Client>(*this, client_id_next_, std::move(request));
    clients_.emplace(client_id_next_++, std::move(client));
    if (exclusive) {
      SetConfiguration(0);
    }
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
  streams_.resize(configurations_[index].streams().size());
  current_configuration_index_ = index;
  FX_LOGS(DEBUG) << "Configuration set to " << index << ".";
  for (auto& client : clients_) {
    client.second->PostConfigurationUpdated(current_configuration_index_);
    client.second->PostMuteUpdated(mute_state_);
  }
}

void DeviceImpl::PostSetSoftwareMuteState(
    bool muted, fuchsia::camera3::Device::SetSoftwareMuteStateCallback callback) {
  zx_status_t status =
      async::PostTask(loop_.dispatcher(), [this, muted, callback = std::move(callback)]() mutable {
        mute_state_.software_muted = muted;
        UpdateControllerStreamingState();
        auto counter = std::make_shared<std::atomic_uint32_t>(streams_.size());
        for (auto& stream : streams_) {
          stream->PostSetMuteState(
              mute_state_, [this, counter, callback = callback.share()]() mutable {
                if (counter->fetch_sub(1) == 1) {
                  async::PostTask(loop_.dispatcher(), [this, callback = callback.share()] {
                    callback();
                    for (auto& client : clients_) {
                      client.second->PostMuteUpdated(mute_state_);
                    }
                  });
                }
              });
        }
      });
  ZX_ASSERT(status == ZX_OK);
}

void DeviceImpl::UpdateControllerStreamingState() {
  if (mute_state_.muted() && controller_streaming_) {
    controller_->DisableStreaming();
    controller_streaming_ = false;
  }
  if (!mute_state_.muted() && !controller_streaming_) {
    controller_->EnableStreaming();
    controller_streaming_ = true;
  }
}

void DeviceImpl::PostConnectToStream(uint32_t index,
                                     fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  ZX_ASSERT(
      async::PostTask(loop_.dispatcher(), [this, index, request = std::move(request)]() mutable {
        ConnectToStream(index, std::move(request));
      }) == ZX_OK);
}

void DeviceImpl::ConnectToStream(uint32_t index,
                                 fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  if (index > streams_.size()) {
    request.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (streams_[index]) {
    request.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }

  // Once the necessary token is received, post a task to send the request to the controller.
  auto on_stream_requested =
      [this, index](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
                    fit::function<void(uint32_t)> max_camping_buffers_callback,
                    uint32_t format_index) {
        FX_LOGS(DEBUG) << "New request for legacy stream.";
        ZX_ASSERT(async::PostTask(
                      loop_.dispatcher(),
                      [this, index, token = std::move(token), request = std::move(request),
                       max_camping_buffers_callback = std::move(max_camping_buffers_callback),
                       format_index]() mutable {
                        OnStreamRequested(index, std::move(token), std::move(request),
                                          std::move(max_camping_buffers_callback), format_index);
                      }) == ZX_OK);
      };

  // When the last client disconnects, post a task to the device thread to destroy the stream.
  auto on_no_clients = [this, index]() {
    ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, index]() { streams_[index] = nullptr; }) ==
              ZX_OK);
  };

  streams_[index] = std::make_unique<StreamImpl>(
      configurations_[current_configuration_index_].streams()[index],
      configs_[current_configuration_index_].stream_configs[index], std::move(request),
      std::move(on_stream_requested), std::move(on_no_clients));
}

// Returns when the provided allocator will likely succeed in allocating a collection with the given
// constraints and some number of other participants. This is necessary prior to a solution for
// fxbug.dev/53305. Returns true iff the wait completed successfully.
bool WaitForFreeSpace(fuchsia::sysmem::AllocatorPtr& allocator_ptr,
                      fuchsia::sysmem::BufferCollectionConstraints constraints) {
  // Incorporate typical client constraints.
  constexpr uint32_t kMaxClientBuffers = 5;
  constexpr uint32_t kBytesPerRowDivisor = 32 * 16;  // GPU-optimal stride.
  constraints.min_buffer_count_for_camping += kMaxClientBuffers;
  for (auto& format_constraints : constraints.image_format_constraints) {
    format_constraints.bytes_per_row_divisor =
        std::max(format_constraints.bytes_per_row_divisor, kBytesPerRowDivisor);
  }

  // Adopt the allocator channel as SyncPtr. This is only safe because the call is performed in the
  // thread owned by the previously associated loop dispatcher, and the entire function is
  // synchronous.
  ZX_ASSERT(async_get_default_dispatcher() == allocator_ptr.dispatcher());
  fuchsia::sysmem::AllocatorSyncPtr allocator;
  allocator.Bind(allocator_ptr.Unbind());

  // Repeatedly attempt allocation as long as it fails due to lack of memory.
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  zx_status_t status = ZX_OK;
  uint32_t num_attempts = 0;
  constexpr uint32_t kMaxAttempts = 10;
  constexpr uint32_t kInitialDelayMs = 200;
  constexpr uint32_t kFinalDelayMs = 1000;
  do {
    fuchsia::sysmem::BufferCollectionSyncPtr collection;
    status = allocator->AllocateNonSharedCollection(collection.NewRequest());
    if (status != ZX_OK) {
      // Skip loop to shorten test execution.
      break;
    }
    collection->SetName(0, "FreeSpaceProbe");
    collection->SetConstraints(true, constraints);

    // After calling SetConstraints, allocation may fail. This results in Wait returning NO_MEMORY
    // followed by channel closure. Because the client may observe these in either order, treat
    // channel closure as if it were NO_MEMORY.
    zx_status_t status_out = ZX_OK;
    status = collection->WaitForBuffersAllocated(&status_out, &buffers);
    if (status == ZX_ERR_PEER_CLOSED) {
      status = ZX_ERR_NO_MEMORY;
    } else {
      ZX_ASSERT(status == ZX_OK);
      status = status_out;
      collection->Close();
    }

    // Free any allocated buffers and wait enough time for them to be freed by sysmem as well.
    collection = nullptr;
    buffers = {};
    uint32_t delay_ms =
        kInitialDelayMs + ((kFinalDelayMs - kInitialDelayMs) * num_attempts) / (kMaxAttempts - 1);
    zx::nanosleep(zx::deadline_after(zx::msec(delay_ms)));
  } while (++num_attempts < kMaxAttempts && status == ZX_ERR_NO_MEMORY);

  // Restore the channel to the async binding.
  zx_status_t restore_status = allocator_ptr.Bind(allocator.Unbind());
  if (restore_status != ZX_OK) {
    ZX_ASSERT(restore_status == ZX_ERR_CANCELED);
    // Thread is shutting down.
    return false;
  }

  if (status != ZX_OK) {
    FX_PLOGS(INFO, status) << "Timeout waiting for free space.";
    return false;
  }

  return true;
}

void DeviceImpl::OnStreamRequested(
    uint32_t index, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
    fit::function<void(uint32_t)> max_camping_buffers_callback, uint32_t format_index) {
  // TODO(fxbug.dev/53305): sysmem should help transition between collections that cannot exist
  // concurrently
  std::unique_lock sysmem_lock(sysmem_mutex_, std::try_to_lock);
  if (!sysmem_lock.owns_lock()) {
    async::PostTask(loop_.dispatcher(),
                    [this, index, token = std::move(token), request = std::move(request),
                     max_camping_buffers_callback = std::move(max_camping_buffers_callback),
                     format_index]() mutable {
                      OnStreamRequested(index, std::move(token), std::move(request),
                                        std::move(max_camping_buffers_callback), format_index);
                    });
    return;
  }
  // Negotiate buffers for this stream.
  // TODO(fxbug.dev/44770): Watch for buffer collection events.
  fuchsia::sysmem::BufferCollectionPtr collection;
  allocator_->BindSharedCollection(std::move(token), collection.NewRequest(loop_.dispatcher()));
  if (!WaitForFreeSpace(allocator_,
                        configs_[current_configuration_index_].stream_configs[index].constraints)) {
    request.Close(ZX_ERR_NO_MEMORY);
    return;
  }
  // Assign friendly names to each buffer for debugging and profiling.
  std::ostringstream oss;
  oss << "camera_c" << current_configuration_index_ << "_s" << index;
  constexpr uint32_t kNamePriority = 30;  // Higher than Scenic but below the maximum.
  collection->SetName(kNamePriority, oss.str());
  collection->SetConstraints(
      true, configs_[current_configuration_index_].stream_configs[index].constraints);
  collection->WaitForBuffersAllocated(
      [this, index, format_index, request = std::move(request),
       max_camping_buffers_callback = std::move(max_camping_buffers_callback),
       collection = std::move(collection), sysmem_lock = std::move(sysmem_lock)](
          zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) mutable {
        sysmem_lock.unlock();
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Failed to allocate buffers for stream.";
          request.Close(status);
          return;
        }

        // Inform the stream of the maxmimum number of buffers it may hand out.
        uint32_t max_camping_buffers =
            buffers.buffer_count - configs_[current_configuration_index_]
                                       .stream_configs[index]
                                       .constraints.min_buffer_count_for_camping;
        max_camping_buffers_callback(max_camping_buffers);

        // Get the legacy stream using the negotiated buffers.
        controller_->CreateStream(current_configuration_index_, index, format_index,
                                  std::move(buffers), std::move(request));

        collection->Close();
      });
}

void DeviceImpl::OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) {
  if (event.has_mic_mute()) {
    mute_state_.hardware_muted = event.mic_mute();
    UpdateControllerStreamingState();
    for (auto& client : clients_) {
      client.second->PostMuteUpdated(mute_state_);
    }
  }
}
