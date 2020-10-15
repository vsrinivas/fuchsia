// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/device_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <sstream>

#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/util.h"
#include "src/lib/fsl/handles/object_info.h"

namespace {

using ConfigPtr = std::unique_ptr<fuchsia::camera2::hal::Config>;

fit::promise<fuchsia::camera2::DeviceInfo> FetchDeviceInfo(
    const fuchsia::camera2::hal::ControllerPtr& controller) {
  fit::bridge<fuchsia::camera2::DeviceInfo> bridge;
  controller->GetDeviceInfo([completer = std::move(bridge.completer)](auto device_info) mutable {
    completer.complete_ok(std::move(device_info));
  });
  return bridge.consumer.promise();
}

fit::promise<std::vector<ConfigPtr>, zx_status_t> FetchConfigs(
    const fuchsia::camera2::hal::ControllerPtr& controller,
    std::vector<ConfigPtr> configs = std::vector<ConfigPtr>()) {
  fit::bridge<ConfigPtr, zx_status_t> bridge;
  controller->GetNextConfig(
      [completer = std::move(bridge.completer)](auto config, auto status) mutable {
        if (status == ZX_OK) {
          completer.complete_ok(std::move(config));
        } else {
          completer.complete_error(status);
        }
      });
  return bridge.consumer.promise().then([&controller, configs = std::move(configs)](
                                            fit::result<ConfigPtr, zx_status_t>& result) mutable
                                        -> fit::promise<std::vector<ConfigPtr>, zx_status_t> {
    if (result.is_ok()) {
      // If we received a config, we need to call FetchConfigs again to get the next config.
      configs.push_back(result.take_value());
      return FetchConfigs(controller, std::move(configs));
    } else if (result.error() == ZX_ERR_STOP) {
      // This means we've already fetched all configs successfully.
      return fit::make_result_promise<std::vector<ConfigPtr>, zx_status_t>(
          fit::ok(std::move(configs)));
    } else {
      // Unexpected zx_status_t values will be a failure.
      return fit::make_result_promise<std::vector<ConfigPtr>, zx_status_t>(
          fit::error(result.take_error()));
    }
  });
}

}  // namespace

DeviceImpl::DeviceImpl(async_dispatcher_t* dispatcher, fit::executor& executor,
                       fuchsia::sysmem::AllocatorHandle allocator, zx::event bad_state_event)
    : dispatcher_(dispatcher),
      executor_(executor),
      sysmem_allocator_(dispatcher, std::move(allocator)),
      bad_state_event_(std::move(bad_state_event)),
      button_listener_binding_(this) {}

DeviceImpl::~DeviceImpl() = default;

fit::promise<std::unique_ptr<DeviceImpl>, zx_status_t> DeviceImpl::Create(
    async_dispatcher_t* dispatcher, fit::executor& executor,
    fuchsia::camera2::hal::ControllerHandle controller, fuchsia::sysmem::AllocatorHandle allocator,
    fuchsia::ui::policy::DeviceListenerRegistryHandle registry, zx::event bad_state_event) {
  auto device = std::make_unique<DeviceImpl>(dispatcher, executor, std::move(allocator),
                                             std::move(bad_state_event));
  ZX_ASSERT(device->controller_.Bind(std::move(controller)) == ZX_OK);

  // Bind the controller interface and get some initial startup information.
  device->controller_.set_error_handler([device = device.get()](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Controller server disconnected during initialization.";
    ZX_ASSERT(device->bad_state_event_.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);
  });

  using DeviceInfoResult = fit::result<>;
  auto device_info_promise =
      FetchDeviceInfo(device->controller_)
          .and_then([device = device.get()](fuchsia::camera2::DeviceInfo& device_info) mutable {
            device->device_info_ = std::move(device_info);
          });
  using FetchConfigsResult = fit::result<void, zx_status_t>;
  auto configs_promise =
      FetchConfigs(device->controller_)
          .then([device = device.get()](fit::result<std::vector<ConfigPtr>, zx_status_t>& result)
                    -> FetchConfigsResult {
            if (result.is_error()) {
              return fit::error(result.error());
            }
            for (const auto& config : result.value()) {
              auto result = Convert(*config);
              if (result.is_error()) {
                FX_PLOGS(ERROR, result.error()) << "Failed to convert config";
                return fit::error(result.error());
              }
              device->configurations_.push_back(result.take_value());
              device->configs_.push_back(std::move(*config));
            }
            device->SetConfiguration(0);
            return fit::ok();
          });

  // Wait for all expected callbacks to occur.
  return fit::join_promises(std::move(device_info_promise), std::move(configs_promise))
      .then([device = std::move(device), registry = std::move(registry)](
                fit::result<std::tuple<DeviceInfoResult, FetchConfigsResult>>& results) mutable
            -> fit::result<std::unique_ptr<DeviceImpl>, zx_status_t> {
        FX_CHECK(results.is_ok());
        if (std::get<1>(results.value()).is_error()) {
          return fit::error(std::get<1>(results.value()).error());
        }
        // Bind the registry interface and register the device as a listener.
        ZX_ASSERT(device->registry_.Bind(std::move(registry)) == ZX_OK);
        device->registry_->RegisterMediaButtonsListener(
            device->button_listener_binding_.NewBinding());

        // Rebind the controller error handler.
        device->controller_.set_error_handler(
            fit::bind_member(device.get(), &DeviceImpl::OnControllerDisconnected));
        return fit::ok(std::move(device));
      });
}

fidl::InterfaceRequestHandler<fuchsia::camera3::Device> DeviceImpl::GetHandler() {
  return fit::bind_member(this, &DeviceImpl::OnNewRequest);
}

void DeviceImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  Bind(std::move(request), true);
}

void DeviceImpl::Bind(fidl::InterfaceRequest<fuchsia::camera3::Device> request, bool exclusive) {
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
}

void DeviceImpl::OnControllerDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Controller disconnected unexpectedly.";
  ZX_ASSERT(bad_state_event_.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);
}

void DeviceImpl::RemoveClient(uint64_t id) { clients_.erase(id); }

void DeviceImpl::SetConfiguration(uint32_t index) {
  streams_.clear();
  streams_.resize(configurations_[index].streams().size());
  stream_to_pending_legacy_stream_request_params_.clear();
  stream_request_sent_to_controller_.clear();
  current_configuration_index_ = index;
  FX_LOGS(DEBUG) << "Configuration set to " << index << ".";
  for (auto& client : clients_) {
    client.second->ConfigurationUpdated(current_configuration_index_);
    client.second->MuteUpdated(mute_state_);
  }
}

void DeviceImpl::SetSoftwareMuteState(
    bool muted, fuchsia::camera3::Device::SetSoftwareMuteStateCallback callback) {
  mute_state_.software_muted = muted;
  UpdateControllerStreamingState();
  for (auto& stream : streams_) {
    stream->SetMuteState(mute_state_);
  }
  callback();
  for (auto& client : clients_) {
    client.second->MuteUpdated(mute_state_);
  }
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

  auto check_token = [this](zx_koid_t token_server_koid, fit::function<void(bool)> callback) {
    sysmem_allocator_.fidl()->ValidateBufferCollectionToken(token_server_koid, std::move(callback));
  };

  // Once the necessary token is received, post a task to send the request to the controller.
  auto on_stream_requested =
      [this, index](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
                    fit::function<void(uint32_t)> max_camping_buffers_callback,
                    uint32_t format_index) {
        FX_LOGS(DEBUG) << "New request for legacy stream.";
        OnStreamRequested(index, std::move(token), std::move(request),
                          std::move(max_camping_buffers_callback), format_index);
      };

  // When the last client disconnects, post a task to the device thread to destroy the stream.
  auto on_no_clients = [this, index]() { streams_[index] = nullptr; };

  streams_[index] = std::make_unique<StreamImpl>(
      dispatcher_, configurations_[current_configuration_index_].streams()[index],
      configs_[current_configuration_index_].stream_configs[index], std::move(request),
      std::move(check_token), std::move(on_stream_requested), std::move(on_no_clients));
}

void DeviceImpl::OnStreamRequested(
    uint32_t index, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
    fit::function<void(uint32_t)> max_camping_buffers_callback, uint32_t format_index) {
  // Assign friendly names to each buffer for debugging and profiling.
  std::ostringstream oss;
  oss << "camera_c" << current_configuration_index_ << "_s" << index;
  executor_.schedule_task(
      sysmem_allocator_
          .SafelyBindSharedCollection(
              std::move(token),
              configs_[current_configuration_index_].stream_configs[index].constraints, oss.str())
          .then([this, index, format_index, request = std::move(request),
                 max_camping_buffers_callback = std::move(max_camping_buffers_callback)](
                    fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>&
                        result) mutable {
            if (result.is_error()) {
              request.Close(result.error());
              return;
            }

            auto buffers = result.take_value();
            // Inform the stream of the maxmimum number of buffers it may hand out.
            uint32_t max_camping_buffers =
                buffers.buffer_count - configs_[current_configuration_index_]
                                           .stream_configs[index]
                                           .constraints.min_buffer_count_for_camping;
            max_camping_buffers_callback(max_camping_buffers);

            // Get the legacy stream using the negotiated buffers.
            stream_to_pending_legacy_stream_request_params_.insert_or_assign(
                index,
                ControllerCreateStreamParams{format_index, std::move(buffers), std::move(request)});
            MaybeConnectToLegacyStreams();
          }));
}

constexpr uint32_t kMaxLegacyStreamRequestRequeueCount = 6;
constexpr zx::duration kLegacyStreamRequestDelay = zx::msec(1500);

// TODO(fxbug.dev/42241): Remove workaround once ordering constraint is removed.
void DeviceImpl::MaybeConnectToLegacyStreams() {
  if (stream_to_pending_legacy_stream_request_params_.empty()) {
    return;
  }

  bool preceding_streams_bound = true;
  for (uint32_t i = 0; i < streams_.size(); ++i) {
    auto it = stream_to_pending_legacy_stream_request_params_.find(i);
    if (it != stream_to_pending_legacy_stream_request_params_.end()) {
      auto& [index, params] = *it;
      if (preceding_streams_bound ||
          params.requeue_count++ == kMaxLegacyStreamRequestRequeueCount) {
        // If all preceding streams are bound, or the threshold requeue count has been reached,
        // immediately send the creation request and delete the pending map element.
        controller_->CreateStream(current_configuration_index_, index, params.format_index,
                                  std::move(params.buffers), std::move(params.request));
        stream_request_sent_to_controller_[index] = true;
        stream_to_pending_legacy_stream_request_params_.erase(it);
      }
    }
    if (!stream_request_sent_to_controller_[i]) {
      preceding_streams_bound = false;
    }
  }

  // If any pending requests still exist, retry after a delay.
  async::PostDelayedTask(
      dispatcher_, [this] { MaybeConnectToLegacyStreams(); }, kLegacyStreamRequestDelay);
}

void DeviceImpl::OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) {
  if (event.has_mic_mute()) {
    mute_state_.hardware_muted = event.mic_mute();
    UpdateControllerStreamingState();
    for (auto& client : clients_) {
      client.second->MuteUpdated(mute_state_);
    }
  }
}
