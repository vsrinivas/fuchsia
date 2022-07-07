// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/usb_device/device_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <sstream>
#include <string>

#include "src/camera/bin/usb_device/messages.h"
#include "src/camera/bin/usb_device/uvc_hack.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera {

DeviceImpl::DeviceImpl(async_dispatcher_t* dispatcher, fpromise::executor& executor,
                       fuchsia::camera::ControlSyncPtr control,
                       fuchsia::sysmem::AllocatorPtr allocator, zx::event bad_state_event)
    : dispatcher_(dispatcher),
      executor_(executor),
      control_(std::move(control)),
      allocator_(std::move(allocator)),
      bad_state_event_(std::move(bad_state_event)),
      button_listener_binding_(this) {}

DeviceImpl::~DeviceImpl() = default;

fpromise::promise<std::unique_ptr<DeviceImpl>, zx_status_t> DeviceImpl::Create(
    async_dispatcher_t* dispatcher, fpromise::executor& executor,
    fuchsia::camera::ControlSyncPtr control, fuchsia::sysmem::AllocatorPtr allocator,
    zx::event bad_state_event) {
  auto device = std::make_unique<DeviceImpl>(dispatcher, executor, std::move(control),
                                             std::move(allocator), std::move(bad_state_event));

  // Construct fake stream properties.
  // TODO(ernesthua) - Should really grab the UVC config list and convert that to stream properties.
  // But not sure if that is really the best design choice for fuchsia.camera3.
  fuchsia::camera3::Configuration2 configuration;
  {
    fuchsia::camera3::StreamProperties2 stream_properties;
    UvcHackGetClientStreamProperties2(&stream_properties);
    configuration.mutable_streams()->push_back(std::move(stream_properties));
  }
  device->configurations_.push_back(std::move(configuration));

  fpromise::promise<std::unique_ptr<DeviceImpl>, zx_status_t> return_promise =
      fpromise::make_promise([device = std::move(device)]() mutable
                             -> fpromise::result<std::unique_ptr<DeviceImpl>, zx_status_t> {
        return fpromise::ok(std::move(device));
      });
  return return_promise;
}

fidl::InterfaceRequestHandler<fuchsia::camera3::Device> DeviceImpl::GetHandler() {
  return fit::bind_member(this, &DeviceImpl::OnNewRequest);
}

void DeviceImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  Bind(std::move(request));
}

void DeviceImpl::Bind(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  bool first_client = clients_.empty();
  auto client = std::make_unique<Client>(*this, client_id_next_, std::move(request));
  auto [it, emplaced] = clients_.emplace(client_id_next_++, std::move(client));
  auto& [id, new_client] = *it;
  if (first_client) {
    SetConfiguration(0);
  } else {
    new_client->ConfigurationUpdated(current_configuration_index_);
  }
}

void DeviceImpl::RemoveClient(uint64_t id) { clients_.erase(id); }

void DeviceImpl::SetConfiguration(uint32_t index) {
  std::vector<fpromise::promise<void, zx_status_t>> deallocation_promises;
  for (auto& event : deallocation_events_) {
    fpromise::bridge<void, zx_status_t> bridge;
    auto wait = std::make_shared<async::WaitOnce>(event.release(), ZX_EVENTPAIR_PEER_CLOSED, 0);
    wait->Begin(dispatcher_, [wait_ref = wait, completer = std::move(bridge.completer)](
                                 async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                 zx_status_t status, const zx_packet_signal_t* signal) mutable {
      if (status != ZX_OK) {
        completer.complete_error(status);
        return;
      }
      completer.complete_ok();
    });
    deallocation_promises.push_back(bridge.consumer.promise());
  }
  deallocation_events_.clear();
  deallocation_promises_ = std::move(deallocation_promises);

  for (auto& stream : streams_) {
    if (stream) {
      stream->CloseAllClients(ZX_OK);
    }
  }

  streams_.clear();
  streams_.resize(configurations_[index].streams().size());
  FX_LOGS(INFO) << "Configuration set to " << index << ".";
  for (auto& client : clients_) {
    client.second->ConfigurationUpdated(current_configuration_index_);
  }
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

  auto on_stream_requested = [this, index](
                                 fuchsia::sysmem::BufferCollectionInfo buffer_collection_info,
                                 fuchsia::camera::FrameRate frame_rate,
                                 fidl::InterfaceRequest<fuchsia::camera::Stream> request,
                                 zx::eventpair driver_token) {
    OnStreamRequested(index, std::move(buffer_collection_info), std::move(frame_rate),
                      std::move(request), std::move(driver_token));
  };

  // When the last client disconnects destroy the stream.
  auto on_no_clients = [this, index]() { streams_[index] = nullptr; };

  auto description =
      "c" + std::to_string(current_configuration_index_) + "s" + std::to_string(index);

  StreamImpl::AllocatorBindSharedCollectionCallback allocator_bind_shared_collection =
      [this](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle,
             fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) {
        AllocatorBindSharedCollection(std::move(token_handle), std::move(request));
      };

  streams_[index] = std::make_unique<StreamImpl>(
      dispatcher_, configurations_[current_configuration_index_].streams()[index],
      std::move(request), std::move(on_stream_requested),
      std::move(allocator_bind_shared_collection), std::move(on_no_clients), description);
}

// This call back function is invoked when the client asks to connect to a camera stream. The call
// back to DeviceImpl is necessary because the control connection is maintained in DeviceImpl.
// Therefore, the call to CreateStream originates here.
void DeviceImpl::OnStreamRequested(uint32_t index,
                                   fuchsia::sysmem::BufferCollectionInfo buffer_collection_info,
                                   fuchsia::camera::FrameRate frame_rate,
                                   fidl::InterfaceRequest<fuchsia::camera::Stream> request,
                                   zx::eventpair driver_token) {
  auto connect_to_stream =
      [this, buffer_collection_info = std::move(buffer_collection_info),
       frame_rate = std::move(frame_rate), request = std::move(request),
       driver_token = std::move(driver_token)](
          const fpromise::result<std::vector<fpromise::result<void, zx_status_t>>>&
              results) mutable {
        std::string description = "c0s0";
        bool deallocation_complete = true;
        if (results.is_error()) {
          FX_LOGS(WARNING) << "aggregate deallocation wait failed prior to connecting to "
                           << description;
          deallocation_complete = false;
        } else {
          auto& wait_results = results.value();
          for (const auto& wait_result : wait_results) {
            if (wait_result.is_error()) {
              FX_PLOGS(WARNING, wait_result.error()) << "wait failed for previous stream at index "
                                                     << &wait_result - wait_results.data();
              deallocation_complete = false;
            }
          }
        }

        // Delayed execution of connecting to USB webcam stream at UVC driver.
        fit::closure connect = [this, buffer_collection_info = std::move(buffer_collection_info),
                                frame_rate = std::move(frame_rate), request = std::move(request),
                                driver_token = std::move(driver_token), description]() mutable {
          FX_LOGS(INFO) << "connecting to controller stream: " << description;
          control_->CreateStream(std::move(buffer_collection_info), std::move(frame_rate),
                                 std::move(request), std::move(driver_token));
        };

        // If the wait for deallocation failed, try to connect anyway after a fixed delay.
        if (!deallocation_complete) {
          constexpr uint32_t kFallbackDelayMsec = 5000;
          FX_LOGS(WARNING) << "deallocation wait failed; falling back to timed wait of "
                           << kFallbackDelayMsec << "ms";
          ZX_ASSERT(async::PostDelayedTask(dispatcher_, std::move(connect),
                                           zx::msec(kFallbackDelayMsec)) == ZX_OK);
          return;
        }
        // Otherwise, connect immediately.
        connect();
      };

  // Wait for any previous configurations buffers to finish deallocation, then connect
  // to stream. The move and clear are necessary to ensure subsequent accesses to the container
  // produces well-defined results.
  auto promises = std::move(deallocation_promises_);
  deallocation_promises_.clear();
  executor_.schedule_task(fpromise::join_promise_vector(std::move(promises))
                              .then(std::move(connect_to_stream))
                              .wrap_with(streams_[index]->Scope()));
}

void DeviceImpl::AllocatorBindSharedCollection(
    fuchsia::sysmem::BufferCollectionTokenHandle token,
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) {
  allocator_->BindSharedCollection(std::move(token), std::move(request));
}

void DeviceImpl::OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) {
  OnEvent(std::move(event), [] {});
}

void DeviceImpl::OnEvent(fuchsia::ui::input::MediaButtonsEvent event,
                         fuchsia::ui::policy::MediaButtonsListener::OnEventCallback callback) {
  callback();
}

}  // namespace camera
