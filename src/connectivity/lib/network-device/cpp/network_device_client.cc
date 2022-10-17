// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_device_client.h"

#include <fcntl.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/device/network.h>
#include <zircon/status.h>

namespace network {
namespace client {

namespace {
// The maximum FIFO depth that this client can handle.
// Set to the maximum number of `uint16`s that a zx FIFO can hold.
constexpr uint64_t kMaxDepth = ZX_PAGE_SIZE / sizeof(uint16_t);

constexpr zx_signals_t kFifoWaitReads = ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED;
constexpr zx_signals_t kFifoWaitWrites = ZX_FIFO_WRITABLE;
}  // namespace

zx::result<DeviceInfo> DeviceInfo::Create(const netdev::wire::DeviceInfo& fidl) {
  if (!(fidl.has_min_descriptor_length() && fidl.has_descriptor_version() && fidl.has_rx_depth() &&
        fidl.has_tx_depth() && fidl.has_buffer_alignment() && fidl.has_min_rx_buffer_length() &&
        fidl.has_min_tx_buffer_length() && fidl.has_min_tx_buffer_head() &&
        fidl.has_min_tx_buffer_tail() && fidl.has_max_buffer_parts())) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  uint32_t max_buffer_length = std::numeric_limits<uint32_t>::max();
  if (fidl.has_max_buffer_length()) {
    max_buffer_length = fidl.max_buffer_length();
    if (max_buffer_length == 0) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  DeviceInfo info = {
      .min_descriptor_length = fidl.min_descriptor_length(),
      .descriptor_version = fidl.descriptor_version(),
      .rx_depth = fidl.rx_depth(),
      .tx_depth = fidl.tx_depth(),
      .buffer_alignment = fidl.buffer_alignment(),
      .max_buffer_length = max_buffer_length,
      .min_rx_buffer_length = fidl.min_rx_buffer_length(),
      .min_tx_buffer_length = fidl.min_tx_buffer_length(),
      .min_tx_buffer_head = fidl.min_tx_buffer_head(),
      .min_tx_buffer_tail = fidl.min_tx_buffer_tail(),
      .max_buffer_parts = fidl.max_buffer_parts(),
  };

  if (fidl.has_rx_accel()) {
    auto& rx_accel = fidl.rx_accel();
    std::copy(rx_accel.begin(), rx_accel.end(), std::back_inserter(info.rx_accel));
  }
  if (fidl.has_tx_accel()) {
    auto& tx_accel = fidl.tx_accel();
    std::copy(tx_accel.begin(), tx_accel.end(), std::back_inserter(info.tx_accel));
  }

  return zx::ok(std::move(info));
}

zx::result<PortInfoAndMac> PortInfoAndMac::Create(
    const netdev::wire::PortInfo& fidl,
    const std::optional<fuchsia_net::wire::MacAddress>& unicast_address) {
  if (!(fidl.has_id() && fidl.has_class())) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  PortInfoAndMac info = {
      .id = fidl.id(),
      .port_class = fidl.class_(),
      .unicast_address = unicast_address,
  };

  if (fidl.has_rx_types()) {
    auto& rx_types = fidl.rx_types();
    std::copy(rx_types.begin(), rx_types.end(), std::back_inserter(info.rx_types));
  }
  if (fidl.has_tx_types()) {
    auto& tx_types = fidl.tx_types();
    std::copy(tx_types.begin(), tx_types.end(), std::back_inserter(info.tx_types));
  }

  return zx::ok(std::move(info));
}

NetworkDeviceClient::NetworkDeviceClient(fidl::ClientEnd<netdev::Device> handle,
                                         async_dispatcher_t* dispatcher)
    : dispatcher_([dispatcher]() {
        if (dispatcher != nullptr) {
          return dispatcher;
        }
        return async_get_default_dispatcher();
      }()),
      device_(std::move(handle), dispatcher_, this),
      executor_(std::make_unique<async::Executor>(dispatcher_)) {}

void NetworkDeviceClient::OnDeviceError(fidl::UnbindInfo info) {
  if (info.status() == ZX_ERR_PEER_CLOSED) {
    FX_LOGS(WARNING) << "device detached";
  } else {
    FX_LOGS(ERROR) << "device handler error: " << info;
  }
  ErrorTeardown(info.status());
}

void NetworkDeviceClient::OnSessionError(fidl::UnbindInfo info) {
  if (info.status() == ZX_ERR_PEER_CLOSED) {
    FX_LOGS(WARNING) << "session detached";
  } else {
    FX_LOGS(ERROR) << "session handler error: " << info;
  }
  ErrorTeardown(info.status());
}

NetworkDeviceClient::~NetworkDeviceClient() = default;

SessionConfig NetworkDeviceClient::DefaultSessionConfig(const DeviceInfo& dev_info) {
  const uint32_t buffer_length = std::min(kDefaultBufferLength, dev_info.max_buffer_length);
  // This allows us to align up without a conditional, as explained here:
  // https://stackoverflow.com/a/9194117
  const uint64_t buffer_stride =
      ((buffer_length + dev_info.buffer_alignment - 1) / dev_info.buffer_alignment) *
      dev_info.buffer_alignment;
  return {
      .buffer_length = buffer_length,
      .buffer_stride = buffer_stride,
      .descriptor_length = sizeof(buffer_descriptor_t),
      .tx_header_length = dev_info.min_tx_buffer_head,
      .tx_tail_length = dev_info.min_tx_buffer_tail,
      .rx_descriptor_count = dev_info.rx_depth,
      .tx_descriptor_count = dev_info.tx_depth,
      .options = netdev::wire::SessionFlags::kPrimary,
  };
}

void NetworkDeviceClient::OpenSession(const std::string& name,
                                      NetworkDeviceClient::OpenSessionCallback callback,
                                      NetworkDeviceClient::SessionConfigFactory config_factory) {
  if (session_running_) {
    callback(ZX_ERR_ALREADY_EXISTS);
    return;
  }
  session_running_ = true;
  fpromise::bridge<DeviceInfo, zx_status_t> bridge;
  device_->GetInfo().ThenExactlyOnce(
      [res = std::move(bridge.completer)](
          fidl::WireUnownedResult<netdev::Device::GetInfo>& result) mutable {
        if (!result.ok()) {
          res.complete_error(result.status());
          return;
        }
        zx::result info = DeviceInfo::Create(result->info);
        if (info.is_error()) {
          res.complete_error(info.status_value());
        } else {
          res.complete_ok(std::move(info.value()));
        }
      });

  auto prepare_session = [this, cfg = std::move(config_factory)](
                             DeviceInfo& info) -> fpromise::result<void, zx_status_t> {
    session_config_ = cfg(info);
    device_info_ = std::move(info);
    zx_status_t status;
    if ((status = PrepareSession()) != ZX_OK) {
      return fpromise::error(status);
    }
    return fpromise::ok();
  };
  auto open_session = [this, name]() -> fpromise::promise<void, zx_status_t> {
    fpromise::bridge<void, zx_status_t> bridge;
    fidl::Arena alloc;
    zx::result session_info = MakeSessionInfo(alloc);
    if (session_info.is_error()) {
      return fpromise::make_error_promise(session_info.error_value());
    }
    device_->OpenSession(fidl::StringView::FromExternal(name), session_info.value())
        .Then([this, res = std::move(bridge.completer)](
                  fidl::WireUnownedResult<netdev::Device::OpenSession>& result) mutable {
          if (!result.ok()) {
            res.complete_error(result.status());
            return;
          }

          const auto* open_result = result.Unwrap();
          if (open_result->is_error()) {
            res.complete_error(open_result->error_value());
          } else {
            netdev::wire::DeviceOpenSessionResponse& response = *open_result->value();
            session_.Bind(std::move(response.session), dispatcher_, this);
            rx_fifo_ = std::move(response.fifos.rx);
            tx_fifo_ = std::move(response.fifos.tx);
            res.complete_ok();
          }
        });
    return bridge.consumer.promise_or(fpromise::error(ZX_ERR_CANCELED));
  };
  auto prepare_descriptors = [this]() -> fpromise::result<void, zx_status_t> {
    zx_status_t status;
    if ((status = PrepareDescriptors()) != ZX_OK) {
      return fpromise::error(status);
    } else {
      return fpromise::ok();
    }
  };
  auto fire_callback = [this,
                        cb = std::move(callback)](fpromise::result<void, zx_status_t>& result) {
    if (result.is_ok()) {
      cb(ZX_OK);
    } else {
      session_running_ = false;
      cb(result.error());
    }
  };
  auto prom = bridge.consumer.promise()
                  .and_then(std::move(prepare_session))
                  .and_then(std::move(open_session))
                  .and_then(std::move(prepare_descriptors))
                  .then(std::move(fire_callback));
  fpromise::schedule_for_consumer(executor_.get(), std::move(prom));
}

zx_status_t SessionConfig::Validate() {
  if (buffer_length <= tx_header_length + tx_tail_length) {
    FX_LOGS(ERROR) << "Invalid buffer length (" << buffer_length
                   << "), too small for requested Tx tail: (" << tx_tail_length << ") + head: ("
                   << tx_header_length << ")";
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t NetworkDeviceClient::PrepareSession() {
  if (session_config_.descriptor_length < sizeof(buffer_descriptor_t) ||
      (session_config_.descriptor_length % sizeof(uint64_t)) != 0) {
    FX_LOGS(ERROR) << "Invalid descriptor length " << session_config_.descriptor_length;
    return ZX_ERR_INVALID_ARGS;
  }

  if (session_config_.rx_descriptor_count > kMaxDepth ||
      session_config_.tx_descriptor_count > kMaxDepth) {
    FX_LOGS(ERROR) << "Invalid descriptor count  " << session_config_.rx_descriptor_count << "/"
                   << session_config_.tx_descriptor_count
                   << ", this client supports a maximum depth of " << kMaxDepth << " descriptors";
    return ZX_ERR_INVALID_ARGS;
  }

  if (session_config_.buffer_stride < session_config_.buffer_length) {
    FX_LOGS(ERROR) << "Stride in VMO can't be smaller than buffer length";
    return ZX_ERR_INVALID_ARGS;
  }

  if (session_config_.buffer_stride % device_info_.buffer_alignment != 0) {
    FX_LOGS(ERROR) << "Buffer stride " << session_config_.buffer_stride
                   << "does not meet buffer alignment requirement: "
                   << device_info_.buffer_alignment;
    return ZX_ERR_INVALID_ARGS;
  }

  descriptor_count_ = session_config_.rx_descriptor_count + session_config_.tx_descriptor_count;
  // Check if sum of descriptor count overflows.
  if (descriptor_count_ < session_config_.rx_descriptor_count ||
      descriptor_count_ < session_config_.tx_descriptor_count) {
    FX_LOGS(ERROR) << "Invalid descriptor count, maximum total descriptors must be less than 2^16";
    return ZX_ERR_INVALID_ARGS;
  }

  if (zx_status_t status = session_config_.Validate(); status != ZX_OK) {
    return status;
  }

  uint64_t data_vmo_size = descriptor_count_ * session_config_.buffer_stride;
  if (zx_status_t status = data_.CreateAndMap(data_vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                              nullptr, &data_vmo_);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create data VMO: " << zx_status_get_string(status);
    return status;
  }

  uint64_t descriptors_vmo_size = descriptor_count_ * session_config_.descriptor_length;
  if (zx_status_t status = descriptors_.CreateAndMap(
          descriptors_vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &descriptors_vmo_);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create descriptors VMO: " << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

void NetworkDeviceClient::AttachPort(netdev::wire::PortId port_id,
                                     std::vector<netdev::wire::FrameType> rx_frame_types,
                                     ErrorCallback callback) {
  auto promise = [this, port_id, &rx_frame_types]() -> fpromise::promise<void, zx_status_t> {
    if (!session_.is_valid()) {
      return fpromise::make_error_promise(ZX_ERR_BAD_STATE);
    }
    fpromise::bridge<void, zx_status_t> bridge;
    session_
        ->Attach(port_id, fidl::VectorView<netdev::wire::FrameType>::FromExternal(rx_frame_types))
        .ThenExactlyOnce([completer = std::move(bridge.completer)](
                             fidl::WireUnownedResult<netdev::Session::Attach>& result) mutable {
          if (!result.ok()) {
            completer.complete_error(result.status());
            return;
          }
          if (result->is_error()) {
            completer.complete_error(result->error_value());
          } else {
            completer.complete_ok();
          }
        });
    return bridge.consumer.promise();
  }();
  ScheduleCallbackPromise(std::move(promise), std::move(callback));
}

void NetworkDeviceClient::DetachPort(netdev::wire::PortId port_id, ErrorCallback callback) {
  auto promise = [this, &port_id]() -> fpromise::promise<void, zx_status_t> {
    if (!session_.is_valid()) {
      return fpromise::make_error_promise(ZX_ERR_BAD_STATE);
    }
    fpromise::bridge<void, zx_status_t> bridge;
    session_->Detach(port_id).ThenExactlyOnce(
        [completer = std::move(bridge.completer)](
            fidl::WireUnownedResult<netdev::Session::Detach>& result) mutable {
          if (!result.ok()) {
            completer.complete_error(result.status());
            return;
          }
          if (result->is_error()) {
            completer.complete_error(result->error_value());
          } else {
            completer.complete_ok();
          }
        });
    return bridge.consumer.promise();
  }();
  ScheduleCallbackPromise(std::move(promise), std::move(callback));
}

void NetworkDeviceClient::GetPortInfoWithMac(netdev::wire::PortId port_id,
                                             PortInfoWithMacCallback callback) {
  struct State {
    PortInfoAndMac result;
    fidl::WireClient<netdev::Port> port_client;
    fidl::WireClient<netdev::MacAddressing> mac_client;
  };
  auto state = std::make_unique<State>();

  // Connect to the requested port.
  zx::result port_endpoints = fidl::CreateEndpoints<netdev::Port>();
  if (port_endpoints.is_error()) {
    callback(zx::error(port_endpoints.error_value()));
    return;
  }
  state->port_client.Bind(std::move(port_endpoints->client), dispatcher_);
  // Connect to the port's MacAddressing interface.
  zx::result mac_endpoints = fidl::CreateEndpoints<netdev::MacAddressing>();
  if (mac_endpoints.is_error()) {
    callback(zx::error(mac_endpoints.error_value()));
    return;
  }
  state->mac_client.Bind(std::move(mac_endpoints->client), dispatcher_);

  // Get the port's information.
  //
  // NB: The GetInfo call on port is written before we pipeline the port to
  // device. That ensures we observe an epitaph in case the port doesn't exist,
  // instead of PEER_CLOSED when attempting to write the GetInfo request on the
  // channel.
  fpromise::bridge<void, zx_status_t> bridge;
  state->port_client->GetInfo().ThenExactlyOnce(
      [completer = std::move(bridge.completer),
       state = state.get()](fidl::WireUnownedResult<netdev::Port::GetInfo>& result) mutable {
        if (!result.ok()) {
          completer.complete_error(result.status());
          return;
        }
        zx::result<PortInfoAndMac> info =
            PortInfoAndMac::Create(result.value().info, /*unicast_address=*/std::nullopt);
        if (!info.is_ok()) {
          completer.complete_error(info.error_value());
          return;
        }
        state->result = std::move(info.value());
        completer.complete_ok();
      });
  const fidl::Status result = device_->GetPort(port_id, std::move(port_endpoints->server));
  if (!result.ok()) {
    callback(zx::error(result.status()));
    return;
  }

  // Get the Mac address of the interface.
  auto get_mac_address =
      [state = state.get(),
       mac_server =
           std::move(mac_endpoints->server)]() mutable -> fpromise::promise<void, zx_status_t> {
    fpromise::bridge<void, zx_status_t> bridge;

    // NB: Like above, the GetUnicastAddress call on mac is written before we
    // pipeline the mac to the port. That ensures we observe an epitaph in case
    // mac is not supported, instead of PEER_CLOSED when attempting to write the
    // GetUnicastAddress request on the channel.
    state->mac_client->GetUnicastAddress().ThenExactlyOnce(
        [completer = std::move(bridge.completer),
         state](fidl::WireUnownedResult<netdev::MacAddressing::GetUnicastAddress>& result) mutable {
          if (!result.ok()) {
            zx_status_t status = result.status();
            if (status == ZX_ERR_NOT_SUPPORTED && result.is_peer_closed()) {
              completer.complete_ok();
            } else {
              completer.complete_error(status);
            }
            return;
          }

          state->result.unicast_address = result->address;
          completer.complete_ok();
        });
    const fidl::Status result = state->port_client->GetMac(std::move(mac_server));
    if (!result.ok()) {
      return fpromise::make_error_promise(result.status());
    }

    return bridge.consumer.promise();
  };

  // Fetch results, and call the user's callback.
  auto fetch_details = bridge.consumer.promise()
                           .and_then(std::move(get_mac_address))
                           .then([callback = std::move(callback), state = state.get()](
                                     fpromise::result<void, zx_status_t>& result) {
                             if (!result.is_ok()) {
                               callback(zx::error(result.error()));
                               return;
                             }
                             callback(zx::success(std::move(state->result)));
                           })
                           // Keep `state` alive until the promise completes.
                           .inspect([state = std::move(state)](const fpromise::result<>&) {});
  fpromise::schedule_for_consumer(executor_.get(), std::move(fetch_details));
}

void NetworkDeviceClient::GetPorts(PortsCallback callback) {
  struct PortWatcherHelper {
    using PortsAndCompleted = std::pair<std::vector<netdev::wire::PortId>, bool>;
    using Promise = fpromise::promise<PortsAndCompleted, zx_status_t>;
    static Promise Watch(fidl::WireClient<netdev::PortWatcher> watcher,
                         std::vector<netdev::wire::PortId> found_ports) {
      fpromise::bridge<PortsAndCompleted, zx_status_t> bridge;
      watcher->Watch().ThenExactlyOnce(
          [completer = std::move(bridge.completer), ports = std::move(found_ports)](
              fidl::WireUnownedResult<netdev::PortWatcher::Watch>& result) mutable {
            if (!result.ok()) {
              completer.complete_error(result.status());
              return;
            }
            const netdev::wire::DevicePortEvent& event = result->event;
            switch (event.Which()) {
              case netdev::wire::DevicePortEvent::Tag::kIdle:
                completer.complete_ok(std::make_pair(std::move(ports), true));
                break;
              case netdev::wire::DevicePortEvent::Tag::kExisting:
                ports.push_back(event.existing());
                completer.complete_ok(std::make_pair(std::move(ports), false));
                break;
              case netdev::wire::DevicePortEvent::Tag::kRemoved:
              case netdev::wire::DevicePortEvent::Tag::kAdded:
                completer.complete_error(ZX_ERR_INTERNAL);
                break;
            }
          });

      return bridge.consumer.promise().and_then(
          [watcher = std::move(watcher)](PortsAndCompleted& next) mutable -> Promise {
            auto& [ports, complete] = next;
            if (complete) {
              // All done.
              return fpromise::make_result_promise<PortsAndCompleted, zx_status_t>(
                  fpromise::ok(std::move(next)));
            }
            return Watch(std::move(watcher), std::move(ports));
          });
    }
  };
  zx::result watcher_endpoints = fidl::CreateEndpoints<netdev::PortWatcher>();
  if (watcher_endpoints.is_error()) {
    callback(zx::error(watcher_endpoints.error_value()));
    return;
  }
  const fidl::Status result = device_->GetPortWatcher(std::move(watcher_endpoints->server));
  if (!result.ok()) {
    callback(zx::error(result.status()));
    return;
  }
  fidl::WireClient<netdev::PortWatcher> watcher;
  watcher.Bind(std::move(watcher_endpoints->client), dispatcher_);

  fpromise::bridge<std::vector<netdev::wire::PortId>, zx_status_t> bridge;
  auto promise = PortWatcherHelper::Watch(std::move(watcher), {});

  auto list_ports = promise.then(
      [callback = std::move(callback)](
          fpromise::result<PortWatcherHelper::PortsAndCompleted, zx_status_t>& result) {
        if (!result.is_ok()) {
          callback(zx::error(result.error()));
          return;
        }
        auto& [ports, complete] = result.value();
        FX_CHECK(complete);
        callback(zx::success(std::move(ports)));
      });

  fpromise::schedule_for_consumer(executor_.get(), std::move(list_ports));
}

void NetworkDeviceClient::ScheduleCallbackPromise(fpromise::promise<void, zx_status_t> promise,
                                                  ErrorCallback callback) {
  fpromise::schedule_for_consumer(
      executor_.get(),
      promise.then([callback = std::move(callback)](fpromise::result<void, zx_status_t>& result) {
        if (result.is_ok()) {
          callback(ZX_OK);
        } else {
          callback(result.error());
        }
      }));
}

zx_status_t NetworkDeviceClient::KillSession() {
  if (!session_.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }
  // Cancel all the waits so we stop fetching frames.
  rx_wait_.Cancel();
  rx_writable_wait_.Cancel();
  tx_wait_.Cancel();
  tx_writable_wait_.Cancel();

  const fidl::Status result = session_->Close();
  if (result.is_peer_closed()) {
    return ZX_OK;
  }
  return result.status();
}

zx::result<std::unique_ptr<NetworkDeviceClient::StatusWatchHandle>>
NetworkDeviceClient::WatchStatus(netdev::wire::PortId port_id, StatusCallback callback,
                                 uint32_t buffer) {
  zx::result port_endpoints = fidl::CreateEndpoints<netdev::Port>();
  if (port_endpoints.is_error()) {
    return port_endpoints.take_error();
  }

  zx::result watcher_endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  if (watcher_endpoints.is_error()) {
    return watcher_endpoints.take_error();
  }
  {
    fidl::Status result = device_->GetPort(port_id, std::move(port_endpoints->server));
    if (!result.ok()) {
      return zx::error(result.status());
    }
  }
  fidl::Status result = fidl::WireCall(port_endpoints->client)
                            ->GetStatusWatcher(std::move(watcher_endpoints->server), buffer);
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok(std::unique_ptr<StatusWatchHandle>(new StatusWatchHandle(
      std::move(watcher_endpoints->client), dispatcher_, std::move(callback))));
}

zx::result<netdev::wire::SessionInfo> NetworkDeviceClient::MakeSessionInfo(fidl::AnyArena& alloc) {
  uint64_t descriptor_length_words = session_config_.descriptor_length / sizeof(uint64_t);
  ZX_DEBUG_ASSERT_MSG(descriptor_length_words <= std::numeric_limits<uint8_t>::max(),
                      "session descriptor length %ld (%ld words) overflows uint8_t",
                      session_config_.descriptor_length, descriptor_length_words);

  netdev::wire::SessionInfo session_info(alloc);
  session_info.set_descriptor_version(NETWORK_DEVICE_DESCRIPTOR_VERSION);
  session_info.set_descriptor_length(static_cast<uint8_t>(descriptor_length_words));
  session_info.set_descriptor_count(descriptor_count_);
  session_info.set_options(session_config_.options);

  zx::vmo data_vmo;
  zx_status_t status;
  if ((status = data_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &data_vmo)) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate data VMO: " << zx_status_get_string(status);
    return zx::error(status);
  }
  session_info.set_data(std::move(data_vmo));

  zx::vmo descriptors_vmo;
  if ((status = descriptors_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &descriptors_vmo)) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate descriptors VMO: " << zx_status_get_string(status);
    return zx::error(status);
  }
  session_info.set_descriptors(std::move(descriptors_vmo));

  return zx::ok(std::move(session_info));
}

buffer_descriptor_t* NetworkDeviceClient::descriptor(uint16_t idx) {
  ZX_ASSERT_MSG(idx < descriptor_count_, "invalid index %d, want < %d", idx, descriptor_count_);
  ZX_ASSERT_MSG(descriptors_.start() != nullptr, "descriptors not mapped");
  return reinterpret_cast<buffer_descriptor_t*>(static_cast<uint8_t*>(descriptors_.start()) +
                                                session_config_.descriptor_length * idx);
}

void* NetworkDeviceClient::data(uint64_t offset) {
  ZX_ASSERT(offset < data_.size());
  return static_cast<uint8_t*>(data_.start()) + offset;
}

void NetworkDeviceClient::ResetRxDescriptor(buffer_descriptor_t* descriptor) {
  *descriptor = {
      .nxt = 0xFFFF,
      .info_type = static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo),
      .offset = descriptor->offset,
      .data_length = session_config_.buffer_length,
  };
}

void NetworkDeviceClient::ResetTxDescriptor(buffer_descriptor_t* descriptor) {
  *descriptor = {
      .nxt = 0xFFFF,
      .info_type = static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo),
      .offset = descriptor->offset,
      .head_length = session_config_.tx_header_length,
      .tail_length = session_config_.tx_tail_length,
      .data_length = session_config_.buffer_length - session_config_.tx_header_length -
                     session_config_.tx_tail_length,
  };
}

zx_status_t NetworkDeviceClient::PrepareDescriptors() {
  uint16_t desc = 0;
  uint64_t buff_off = 0;
  auto* pDesc = static_cast<uint8_t*>(descriptors_.start());
  rx_out_queue_.reserve(session_config_.rx_descriptor_count);
  tx_out_queue_.reserve(session_config_.tx_descriptor_count);

  for (; desc < session_config_.rx_descriptor_count; desc++) {
    auto* descriptor = reinterpret_cast<buffer_descriptor_t*>(pDesc);
    descriptor->offset = buff_off;
    ResetRxDescriptor(descriptor);

    buff_off += session_config_.buffer_stride;
    pDesc += session_config_.descriptor_length;
    rx_out_queue_.push_back(desc);
  }
  for (; desc < descriptor_count_; desc++) {
    auto* descriptor = reinterpret_cast<buffer_descriptor_t*>(pDesc);
    ResetTxDescriptor(descriptor);
    descriptor->offset = buff_off;

    buff_off += session_config_.buffer_stride;
    pDesc += session_config_.descriptor_length;
    tx_avail_.push(desc);
  }
  rx_wait_.set_object(rx_fifo_.get());
  rx_wait_.set_trigger(kFifoWaitReads);
  ZX_ASSERT(rx_wait_.Begin(dispatcher_) == ZX_OK);
  tx_wait_.set_object(tx_fifo_.get());
  tx_wait_.set_trigger(kFifoWaitReads);
  ZX_ASSERT(tx_wait_.Begin(dispatcher_) == ZX_OK);
  rx_writable_wait_.set_object(rx_fifo_.get());
  rx_writable_wait_.set_trigger(kFifoWaitWrites);
  tx_writable_wait_.set_object(tx_fifo_.get());
  tx_writable_wait_.set_trigger(kFifoWaitWrites);

  FlushRx();

  return ZX_OK;
}

void NetworkDeviceClient::FlushRx() {
  size_t flush = std::min(rx_out_queue_.size(), static_cast<size_t>(device_info_.rx_depth));
  ZX_ASSERT(flush != 0);

  // TODO(https://fxbug.dev/32098): We're assuming that writing to the FIFO here
  // is a sufficient memory barrier for the other end to access the data. That
  // is currently true but not really guaranteed by the API.
  zx_status_t status = rx_fifo_.write(sizeof(uint16_t), rx_out_queue_.data(), flush, &flush);
  bool sched_more;
  if (status == ZX_OK) {
    rx_out_queue_.erase(rx_out_queue_.begin(), rx_out_queue_.begin() + flush);
    sched_more = !rx_out_queue_.empty();
  } else {
    sched_more = status == ZX_ERR_SHOULD_WAIT;
  }

  if (sched_more && !rx_writable_wait_.is_pending()) {
    ZX_ASSERT(rx_writable_wait_.Begin(dispatcher_) == ZX_OK);
  }
}

void NetworkDeviceClient::FlushTx() {
  size_t flush = std::min(tx_out_queue_.size(), static_cast<size_t>(device_info_.tx_depth));
  ZX_ASSERT(flush != 0);

  // TODO(https://fxbug.dev/32098): We're assuming that writing to the FIFO here
  // is a sufficient memory barrier for the other end to access the data. That
  // is currently true but not really guaranteed by the API.
  zx_status_t status = tx_fifo_.write(sizeof(uint16_t), tx_out_queue_.data(), flush, &flush);
  bool sched_more;
  if (status == ZX_OK) {
    tx_out_queue_.erase(tx_out_queue_.begin(), tx_out_queue_.begin() + flush);
    sched_more = !tx_out_queue_.empty();
  } else {
    sched_more = status == ZX_ERR_SHOULD_WAIT;
  }

  if (sched_more && !tx_writable_wait_.is_pending()) {
    ZX_ASSERT(tx_writable_wait_.Begin(dispatcher_) == ZX_OK);
  }
}

void NetworkDeviceClient::ErrorTeardown(zx_status_t err) {
  session_running_ = false;
  data_.Unmap();
  data_vmo_.reset();
  descriptors_.Unmap();
  descriptors_vmo_.reset();
  session_ = {};
  auto cancel_wait = [](async::WaitBase& wait, const char* name) {
    zx_status_t status = wait.Cancel();
    switch (status) {
      case ZX_OK:
      case ZX_ERR_NOT_FOUND:
        break;
      default:
        FX_PLOGS(ERROR, status) << "failed to cancel" << name;
    }
  };
  cancel_wait(tx_wait_, "tx_wait");
  cancel_wait(rx_wait_, "rx_wait");
  cancel_wait(tx_writable_wait_, "tx_writable_wait");
  cancel_wait(rx_writable_wait_, "rx_writable_wait");

  if (err_callback_) {
    err_callback_(err);
  }
}

void NetworkDeviceClient::TxSignal(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "tx wait failed: " << zx_status_get_string(status);
    return;
  }
  if (signal->observed & wait->trigger() & ZX_FIFO_PEER_CLOSED) {
    FX_LOGS(WARNING) << "tx fifo was closed";
    ErrorTeardown(ZX_ERR_PEER_CLOSED);
    return;
  }
  if (signal->observed & wait->trigger() & ZX_FIFO_READABLE) {
    FetchTx();
  }
  if ((signal->observed & wait->trigger() & ZX_FIFO_WRITABLE) && !tx_out_queue_.empty()) {
    FlushTx();
  }

  if (wait != &tx_writable_wait_ || !tx_out_queue_.empty()) {
    ZX_ASSERT(wait->Begin(dispatcher_) == ZX_OK);
  }
}

void NetworkDeviceClient::RxSignal(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "rx wait failed: " << zx_status_get_string(status);
    return;
  }

  if (signal->observed & wait->trigger() & ZX_FIFO_PEER_CLOSED) {
    FX_LOGS(WARNING) << "rx fifo was closed";
    ErrorTeardown(ZX_ERR_PEER_CLOSED);
    return;
  }

  if (signal->observed & wait->trigger() & ZX_FIFO_READABLE) {
    FetchRx();
  }

  if ((signal->observed & wait->trigger() & ZX_FIFO_WRITABLE) && !rx_out_queue_.empty()) {
    FlushRx();
  }

  if (wait != &rx_writable_wait_ || !rx_out_queue_.empty()) {
    ZX_ASSERT(wait->Begin(dispatcher_) == ZX_OK);
  }
}

void NetworkDeviceClient::FetchRx() {
  uint16_t buff[kMaxDepth];
  size_t read;
  zx_status_t status;
  if ((status = rx_fifo_.read(sizeof(uint16_t), buff, kMaxDepth, &read)) != ZX_OK) {
    FX_LOGS(ERROR) << "Error reading from rx queue: " << zx_status_get_string(status);
    return;
  }
  uint16_t* desc_idx = buff;
  while (read > 0) {
    if (rx_callback_) {
      rx_callback_(Buffer(this, *desc_idx, true));
    } else {
      ReturnRxDescriptor(*desc_idx);
    }

    read--;
    desc_idx++;
  }
}

zx_status_t NetworkDeviceClient::Send(NetworkDeviceClient::Buffer* buffer) {
  if (!buffer->is_valid()) {
    return ZX_ERR_UNAVAILABLE;
  }
  if (buffer->rx_) {
    // If this is an RX buffer, we need to get a TX buffer from the pool and return it as an RX
    // buffer in place of this.
    auto tx_buffer = AllocTx();
    if (!tx_buffer.is_valid()) {
      return ZX_ERR_NO_RESOURCES;
    }
    // Flip the buffer, it'll be returned to the rx queue on destruction.
    tx_buffer.rx_ = true;
    buffer->rx_ = false;
  }
  if (!tx_writable_wait_.is_pending()) {
    zx_status_t status = tx_writable_wait_.Begin(dispatcher_);
    if (status != ZX_OK) {
      return status;
    }
  }
  tx_out_queue_.push_back(buffer->descriptor_);

  // Don't return this buffer on destruction.
  // Also invalidate it.
  buffer->parent_ = nullptr;
  return ZX_OK;
}

void NetworkDeviceClient::ReturnTxDescriptor(uint16_t idx) {
  auto* desc = descriptor(idx);
  if (desc->chain_length != 0) {
    ReturnTxDescriptor(desc->nxt);
  }
  ResetTxDescriptor(desc);
  tx_avail_.push(idx);
}

void NetworkDeviceClient::ReturnRxDescriptor(uint16_t idx) {
  auto* desc = descriptor(idx);
  if (desc->chain_length != 0) {
    ReturnRxDescriptor(desc->nxt);
  }
  ResetRxDescriptor(desc);
  rx_out_queue_.push_back(idx);
  if (!rx_writable_wait_.is_pending()) {
    ZX_ASSERT(rx_writable_wait_.Begin(dispatcher_) == ZX_OK);
  }
}

void NetworkDeviceClient::FetchTx() {
  uint16_t buff[kMaxDepth];
  size_t read;
  zx_status_t status;
  if ((status = tx_fifo_.read(sizeof(uint16_t), buff, kMaxDepth, &read)) != ZX_OK) {
    FX_LOGS(ERROR) << "Error reading from tx queue: " << zx_status_get_string(status);
    return;
  }
  uint16_t* desc_idx = buff;
  while (read > 0) {
    // TODO count and log tx errors
    ReturnTxDescriptor(*desc_idx);
    read--;
    desc_idx++;
  }
}

NetworkDeviceClient::Buffer NetworkDeviceClient::AllocTx() {
  if (tx_avail_.empty()) {
    return Buffer();
  } else {
    auto idx = tx_avail_.front();
    tx_avail_.pop();
    return Buffer(this, idx, false);
  }
}

NetworkDeviceClient::Buffer::Buffer() : parent_(nullptr), descriptor_(0), rx_(false) {}

NetworkDeviceClient::Buffer::Buffer(NetworkDeviceClient* parent, uint16_t descriptor, bool rx)
    : parent_(parent), descriptor_(descriptor), rx_(rx) {}

NetworkDeviceClient::Buffer::Buffer(NetworkDeviceClient::Buffer&& other) noexcept
    : parent_(other.parent_),
      descriptor_(other.descriptor_),
      rx_(other.rx_),
      data_(std::move(other.data_)) {
  other.parent_ = nullptr;
}

NetworkDeviceClient::Buffer::~Buffer() {
  if (parent_) {
    if (rx_) {
      parent_->ReturnRxDescriptor(descriptor_);
    } else {
      parent_->ReturnTxDescriptor(descriptor_);
    }
  }
}

NetworkDeviceClient::BufferData& NetworkDeviceClient::Buffer::data() {
  ZX_ASSERT(is_valid());
  if (!data_.is_loaded()) {
    data_.Load(parent_, descriptor_);
  }
  return data_;
}

const NetworkDeviceClient::BufferData& NetworkDeviceClient::Buffer::data() const {
  ZX_ASSERT(is_valid());
  if (!data_.is_loaded()) {
    data_.Load(parent_, descriptor_);
  }
  return data_;
}

zx_status_t NetworkDeviceClient::Buffer::Send() {
  if (!is_valid()) {
    return ZX_ERR_UNAVAILABLE;
  }
  zx_status_t status = data_.PadTo(parent_->device_info_.min_tx_buffer_length);
  if (status != ZX_OK) {
    return status;
  }
  return parent_->Send(this);
}

void NetworkDeviceClient::BufferData::Load(NetworkDeviceClient* parent, uint16_t idx) {
  auto* desc = parent->descriptor(idx);
  while (desc) {
    auto& cur = parts_[parts_count_];
    cur.base_ = parent->data(desc->offset + desc->head_length);
    cur.desc_ = desc;
    parts_count_++;
    if (desc->chain_length != 0) {
      desc = parent->descriptor(desc->nxt);
    } else {
      desc = nullptr;
    }
  }
}

NetworkDeviceClient::BufferRegion& NetworkDeviceClient::BufferData::part(size_t idx) {
  ZX_ASSERT(idx < parts_count_);
  return parts_[idx];
}

const NetworkDeviceClient::BufferRegion& NetworkDeviceClient::BufferData::part(size_t idx) const {
  ZX_ASSERT(idx < parts_count_);
  return parts_[idx];
}

uint32_t NetworkDeviceClient::BufferData::len() const {
  uint32_t c = 0;
  for (uint32_t i = 0; i < parts_count_; i++) {
    c += parts_[i].len();
  }
  return c;
}

netdev::wire::FrameType NetworkDeviceClient::BufferData::frame_type() const {
  return static_cast<netdev::wire::FrameType>(part(0).desc_->frame_type);
}

void NetworkDeviceClient::BufferData::SetFrameType(netdev::wire::FrameType type) {
  part(0).desc_->frame_type = static_cast<uint8_t>(type);
}

netdev::wire::PortId NetworkDeviceClient::BufferData::port_id() const {
  const buffer_descriptor_t& desc = *part(0).desc_;
  return {
      .base = desc.port_id.base,
      .salt = desc.port_id.salt,
  };
}

void NetworkDeviceClient::BufferData::SetPortId(netdev::wire::PortId port_id) {
  buffer_descriptor_t& desc = *part(0).desc_;
  desc.port_id = {
      .base = port_id.base,
      .salt = port_id.salt,
  };
}

netdev::wire::InfoType NetworkDeviceClient::BufferData::info_type() const {
  return static_cast<netdev::wire::InfoType>(part(0).desc_->frame_type);
}

uint32_t NetworkDeviceClient::BufferData::inbound_flags() const {
  return part(0).desc_->inbound_flags;
}

uint32_t NetworkDeviceClient::BufferData::return_flags() const {
  return part(0).desc_->return_flags;
}

void NetworkDeviceClient::BufferData::SetTxRequest(netdev::wire::TxFlags tx_flags) {
  part(0).desc_->inbound_flags = static_cast<uint32_t>(tx_flags);
}

size_t NetworkDeviceClient::BufferData::Write(const void* src, size_t len) {
  const auto* ptr = static_cast<const uint8_t*>(src);
  size_t written = 0;
  for (uint32_t i = 0; i < parts_count_; i++) {
    auto& part = parts_[i];
    uint32_t wr = std::min(static_cast<uint32_t>(len - written), part.len());
    part.Write(ptr, wr);
    ptr += wr;
    written += wr;
  }
  return written;
}

size_t NetworkDeviceClient::BufferData::Write(const BufferData& data) {
  size_t count = 0;

  size_t idx_me = 0;
  size_t offset_me = 0;
  size_t offset_other = 0;
  for (size_t idx_o = 0; idx_o < data.parts_count_ && idx_me < parts_count_;) {
    size_t wr = parts_[idx_me].Write(offset_me, data.parts_[idx_o], offset_other);
    offset_me += wr;
    offset_other += wr;
    count += wr;
    if (offset_me >= parts_[idx_me].len()) {
      idx_me++;
      offset_me = 0;
    }
    if (offset_other >= data.parts_[idx_o].len()) {
      idx_o++;
      offset_other = 0;
    }
  }
  // Update the length on the last descriptor.
  if (idx_me < parts_count_) {
    ZX_DEBUG_ASSERT(offset_me <= std::numeric_limits<uint32_t>::max());
    parts_[idx_me].CapLength(static_cast<uint32_t>(offset_me));
  }

  return count;
}

zx_status_t NetworkDeviceClient::BufferData::PadTo(size_t size) {
  size_t total_size = 0;
  for (uint32_t i = 0; i < parts_count_ && total_size < size; i++) {
    total_size += parts_[i].PadTo(size - total_size);
  }
  if (total_size < size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  return ZX_OK;
}

size_t NetworkDeviceClient::BufferData::Read(void* dst, size_t len) const {
  auto* ptr = static_cast<uint8_t*>(dst);
  size_t actual = 0;
  for (uint32_t i = 0; i < parts_count_ && len > 0; i++) {
    auto& part = parts_[i];
    size_t rd = part.Read(ptr, len);
    len -= rd;
    ptr += rd;
    actual += rd;
  }
  return actual;
}

void NetworkDeviceClient::BufferRegion::CapLength(uint32_t len) {
  if (len <= desc_->data_length) {
    desc_->tail_length += desc_->data_length - len;
    desc_->data_length = len;
  }
}

uint32_t NetworkDeviceClient::BufferRegion::len() const { return desc_->data_length; }

cpp20::span<uint8_t> NetworkDeviceClient::BufferRegion::data() {
  return cpp20::span(static_cast<uint8_t*>(base_), len());
}

cpp20::span<const uint8_t> NetworkDeviceClient::BufferRegion::data() const {
  return cpp20::span(static_cast<const uint8_t*>(base_), len());
}

size_t NetworkDeviceClient::BufferRegion::Write(const void* src, size_t len, size_t offset) {
  uint32_t nlen = std::min(desc_->data_length, static_cast<uint32_t>(len + offset));
  CapLength(nlen);
  std::copy_n(static_cast<const uint8_t*>(src), this->len() - offset, data().begin() + offset);
  return this->len();
}

size_t NetworkDeviceClient::BufferRegion::Read(void* dst, size_t len, size_t offset) const {
  if (offset >= desc_->data_length) {
    return 0;
  }
  len = std::min(len, desc_->data_length - offset);
  std::copy_n(data().begin() + offset, len, static_cast<uint8_t*>(dst));
  return len;
}

size_t NetworkDeviceClient::BufferRegion::Write(size_t offset, const BufferRegion& src,
                                                size_t src_offset) {
  if (offset >= desc_->data_length || src_offset >= src.desc_->data_length) {
    return 0;
  }
  size_t wr = std::min(desc_->data_length - offset, src.desc_->data_length - src_offset);
  std::copy_n(src.data().begin() + src_offset, wr, data().begin() + offset);
  return wr;
}

size_t NetworkDeviceClient::BufferRegion::PadTo(size_t size) {
  if (size > desc_->data_length) {
    size -= desc_->data_length;
    cpp20::span<uint8_t> pad(static_cast<uint8_t*>(base_) + desc_->head_length + desc_->data_length,
                             std::min(size, static_cast<size_t>(desc_->tail_length)));
    memset(pad.data(), 0x00, pad.size());
    desc_->data_length += pad.size();
    desc_->tail_length -= pad.size();
  }
  return desc_->data_length;
}

void NetworkDeviceClient::StatusWatchHandle::Watch() {
  watcher_->WatchStatus().Then(
      [this](fidl::WireUnownedResult<netdev::StatusWatcher::WatchStatus>& result) {
        if (!result.ok()) {
          return;
        }
        callback_(result->port_status);
        // Watch again, we only stop watching when StatusWatchHandle is destroyed.
        Watch();
      });
}

}  // namespace client
}  // namespace network
