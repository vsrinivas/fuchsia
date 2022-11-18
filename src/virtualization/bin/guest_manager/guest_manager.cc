// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_manager.h"

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <unordered_set>

#include <src/lib/files/file.h>

#include "src/virtualization/bin/guest_manager/memory_pressure_handler.h"

namespace {

// This is a locally administered MAC address (first byte 0x02) mixed with the
// Google Organizationally Unique Identifier (00:1a:11). The host gets ff:ff:ff
// and the guest gets 00:00:00 for the last three octets.
constexpr fuchsia::hardware::ethernet::MacAddress kGuestMacAddress = {
    .octets = {0x02, 0x1a, 0x11, 0x00, 0x01, 0x00},
};

uint64_t GetDefaultGuestMemory() {
  const uint64_t host_memory = zx_system_get_physmem();
  const uint64_t max_reserved_host_memory = 3 * (1ul << 30);  // 3 GiB.

  // Reserve half the host memory up to 2 GiB, and allow the rest to be used by the guest.
  return host_memory - std::min(host_memory / 2, max_reserved_host_memory);
}

uint8_t GetDefaultNumCpus() {
  return static_cast<uint8_t>(std::min(zx_system_get_num_cpus(), UINT8_MAX));
}

}  // namespace

using ::fuchsia::virtualization::GuestConfig;
using ::fuchsia::virtualization::GuestDescriptor;
using ::fuchsia::virtualization::GuestError;
using ::fuchsia::virtualization::GuestLifecycle_Create_Result;
using ::fuchsia::virtualization::GuestLifecycle_Run_Result;
using ::fuchsia::virtualization::GuestManagerError;
using ::fuchsia::virtualization::GuestStatus;

GuestManager::GuestManager(async_dispatcher_t* dispatcher, sys::ComponentContext* context,
                           std::string config_pkg_dir_path, std::string config_path)
    : dispatcher_(dispatcher),
      context_(context),
      config_pkg_dir_path_(std::move(config_pkg_dir_path)),
      config_path_(std::move(config_path)) {
  context_->outgoing()->AddPublicService(manager_bindings_.GetHandler(this));
}

fit::result<GuestManagerError, GuestConfig> GuestManager::GetDefaultGuestConfig() {
  // Reads guest config from [zircon|termina|debian]_guest package provided as child in
  // [zircon|termina|debian]_guest_manager component hierarchy.
  const std::string config_path = config_pkg_dir_path_ + config_path_;
  auto open_at = [&](const std::string& path, fidl::InterfaceRequest<fuchsia::io::File> file) {
    return fdio_open((config_pkg_dir_path_ + path).c_str(),
                     static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_READABLE),
                     file.TakeChannel().release());
  };

  std::string content;
  bool readFileSuccess = files::ReadFileToString(config_path, &content);
  if (!readFileSuccess) {
    FX_LOGS(ERROR) << "Failed to read guest configuration " << config_path;
    return fit::error(GuestManagerError::BAD_CONFIG);
  }
  auto config = guest_config::ParseConfig(content, std::move(open_at));
  if (config.is_error()) {
    FX_PLOGS(ERROR, config.error_value()) << "Failed to parse guest configuration " << config_path;
    return fit::error(GuestManagerError::BAD_CONFIG);
  }

  return fit::ok(std::move(*config));
}

// |fuchsia::virtualization::GuestManager|
void GuestManager::Launch(GuestConfig user_config,
                          fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller,
                          LaunchCallback callback) {
  if (is_guest_started()) {
    callback(fpromise::error(GuestManagerError::ALREADY_RUNNING));
    return;
  }

  if (!lifecycle_.is_bound()) {
    // Opening the lifecycle channel will start the VMM component, and closing the channel will
    // destroy the VMM component.
    //
    // This error handler is only invoked if the server side of the channel is closed, not if the
    // guest manager closes the channel to destroy the VMM component. As the VMM component will
    // never intentionally close the channel, this channel closing means that the component has
    // terminated unexpectedly.
    context_->svc()->Connect(lifecycle_.NewRequest());
    lifecycle_.set_error_handler([this](zx_status_t) {
      state_ = GuestStatus::VMM_UNEXPECTED_TERMINATION;
      OnGuestStopped();
    });
  }

  auto default_config = GetDefaultGuestConfig();
  if (default_config.is_error()) {
    callback(fpromise::error(default_config.error_value()));
    return;
  }

  // Use the static config as a base, but apply the user config as an override.
  GuestConfig merged_cfg;
  merged_cfg = guest_config::MergeConfigs(std::move(*default_config), std::move(user_config));
  if (!merged_cfg.has_guest_memory()) {
    merged_cfg.set_guest_memory(GetDefaultGuestMemory());
  }
  if (!merged_cfg.has_cpus()) {
    merged_cfg.set_cpus(GetDefaultNumCpus());
  }

  if (merged_cfg.has_default_net() && merged_cfg.default_net()) {
    merged_cfg.mutable_net_devices()->push_back({
        .mac_address = kGuestMacAddress,
        .enable_bridge = true,
    });
  }

  // Merge the command-line additions into the main kernel command-line field.
  for (auto& cmdline : *merged_cfg.mutable_cmdline_add()) {
    merged_cfg.mutable_cmdline()->append(" " + cmdline);
  }
  merged_cfg.clear_cmdline_add();

  // If there are any initial vsock listeners, they must be bound to unique host ports.
  if (merged_cfg.has_vsock_listeners() && merged_cfg.vsock_listeners().size() > 1) {
    std::unordered_set<uint32_t> ports;
    for (auto& listener : merged_cfg.vsock_listeners()) {
      if (!ports.insert(listener.port).second) {
        callback(fpromise::error(GuestManagerError::BAD_CONFIG));
        return;
      }
    }
  }

  start_time_ = zx::clock::get_monotonic();
  state_ = GuestStatus::STARTING;
  last_error_ = std::nullopt;
  SnapshotConfig(merged_cfg);
  bool balloon_enabled = merged_cfg.has_virtio_balloon();
  lifecycle_->Create(std::move(merged_cfg),
                     [this, controller = std::move(controller), callback = std::move(callback),
                      balloon_enabled](GuestLifecycle_Create_Result result) mutable {
                       this->HandleCreateResult(std::move(result), std::move(controller),
                                                balloon_enabled, std::move(callback));
                     });
}

void GuestManager::HandleCreateResult(
    GuestLifecycle_Create_Result result,
    fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller, bool balloon_enabled,
    LaunchCallback callback) {
  if (result.is_err()) {
    HandleGuestStopped(fit::error(result.err()));
    callback(fpromise::error(GuestManagerError::START_FAILURE));
  } else {
    state_ = GuestStatus::RUNNING;
    lifecycle_->Run(
        [this](GuestLifecycle_Run_Result result) { this->HandleRunResult(std::move(result)); });
    context_->svc()->Connect(std::move(controller));
    if (balloon_enabled) {
      memory_pressure_handler_ = std::make_unique<MemoryPressureHandler>(dispatcher_);
      zx_status_t status = memory_pressure_handler_->Start(context_);
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Failed to start memory pressure handler";
      }
    }
    OnGuestLaunched();
    callback(fpromise::ok());
  }
}

void GuestManager::HandleRunResult(GuestLifecycle_Run_Result result) {
  if (result.is_response()) {
    HandleGuestStopped(fit::ok());
  } else {
    HandleGuestStopped(fit::error(result.err()));
  }
}

void GuestManager::HandleGuestStopped(fit::result<GuestError> err) {
  if (err.is_ok()) {
    last_error_ = std::nullopt;
  } else {
    last_error_ = err.error_value();
  }

  stop_time_ = zx::clock::get_monotonic();
  state_ = GuestStatus::STOPPED;

  OnGuestStopped();
}

void GuestManager::ForceShutdown(ForceShutdownCallback callback) {
  if (!lifecycle_.is_bound() || !is_guest_started()) {
    // VMM component isn't running.
    callback();
    return;
  }

  state_ = GuestStatus::STOPPING;
  lifecycle_->Stop([callback = std::move(callback)]() { callback(); });
}

GuestNetworkState GuestManager::QueryGuestNetworkState() {
  if (!guest_descriptor_.has_networks() || guest_descriptor_.networks().empty()) {
    return GuestNetworkState::NO_NETWORK_DEVICE;
  }

  ::fuchsia::net::interfaces::StateSyncPtr state;
  zx_status_t status = context_->svc()->Connect(state.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to connect to network interface service";
    return GuestNetworkState::FAILED_TO_QUERY;
  }

  ::fuchsia::net::interfaces::WatcherSyncPtr watcher;
  status = state->GetWatcher(::fuchsia::net::interfaces::WatcherOptions(), watcher.NewRequest());
  if (status != ZX_OK || !watcher.is_bound()) {
    FX_PLOGS(ERROR, status) << "Failed to bind to network watcher service";
    return GuestNetworkState::FAILED_TO_QUERY;
  }

  bool has_bridge = false, has_ethernet = false, has_wlan = false;
  uint32_t num_virtual = 0;
  ::fuchsia::net::interfaces::Event event;
  do {
    status = watcher->Watch(&event);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to watch for interface event";
      return GuestNetworkState::FAILED_TO_QUERY;
    }

    if (!event.is_existing()) {
      // Only care about existing interfaces at the moment of this query.
      continue;
    }

    if (!event.existing().has_device_class() || !event.existing().device_class().is_device()) {
      // Ignore loopback interfaces.
      continue;
    }

    if (!event.existing().has_online() || !event.existing().online()) {
      // Only consider enabled interfaces.
      continue;
    }

    switch (event.existing().device_class().device()) {
      case ::fuchsia::hardware::network::DeviceClass::VIRTUAL:
        num_virtual++;
        break;
      case ::fuchsia::hardware::network::DeviceClass::ETHERNET:
        has_ethernet = true;
        break;
      case ::fuchsia::hardware::network::DeviceClass::WLAN:
        has_wlan = true;
        break;
      case ::fuchsia::hardware::network::DeviceClass::BRIDGE:
        has_bridge = true;
        break;
      case ::fuchsia::hardware::network::DeviceClass::PPP:
      case ::fuchsia::hardware::network::DeviceClass::WLAN_AP:
        // Ignore.
        break;
    }
  } while (!event.is_idle());

  if (!has_ethernet && !has_wlan) {
    // No usable host networking, so there won't be any functional guest networking.
    return GuestNetworkState::NO_HOST_NETWORKING;
  }

  if (num_virtual < guest_descriptor_.networks().size()) {
    // Something went wrong during virtio-net device initialization, and there are fewer virtual
    // interfaces than there should be. This is an unlikely state as virtual interfaces may be
    // non-functional, but they should at least be present.
    return GuestNetworkState::MISSING_VIRTUAL_INTERFACES;
  }

  // See if a bridge is expected from the guest network configurations.
  bool expected_bridge =
      std::any_of(guest_descriptor_.networks().begin(), guest_descriptor_.networks().end(),
                  [](auto& spec) { return spec.enable_bridge; });

  if (expected_bridge && !has_bridge) {
    // A bridge was expected from the guest network configurations, but none are present.
    if (has_wlan && !has_ethernet) {
      // There's no ethernet interface to bridge against, but there is a WLAN interface. Bridging
      // against WLAN isn't supported, so the user needs to disconnect from WiFi and connect
      // ethernet.
      return GuestNetworkState::ATTEMPTED_TO_BRIDGE_WITH_WLAN;
    }

    // Possibly a transient state where a bridge is still being created.
    return GuestNetworkState::NO_BRIDGE_CREATED;
  }

  // The host and guest are likely correctly configured for guest networking.
  return GuestNetworkState::OK;
}

// Static.
std::string GuestManager::GuestNetworkStateToStringExplanation(GuestNetworkState state) {
  switch (state) {
    case GuestNetworkState::OK:
      return "Guest network likely configured correctly; "
             "check host connectivity if suspected network failure";
    case GuestNetworkState::NO_NETWORK_DEVICE:
      return "Guest not configured with a network device; "
             "check guest configuration if networking is required";
    case GuestNetworkState::FAILED_TO_QUERY:
      return "Failed to query guest network status";
    case GuestNetworkState::NO_HOST_NETWORKING:
      return "Host has no network interfaces; guest networking will not work";
    case GuestNetworkState::MISSING_VIRTUAL_INTERFACES:
      return "Fewer than expected virtual interfaces; guest failed network device startup";
    case GuestNetworkState::NO_BRIDGE_CREATED:
      return "No bridge between guest and host network interaces; "
             "this may be transient so retrying is recommended";
    case GuestNetworkState::ATTEMPTED_TO_BRIDGE_WITH_WLAN:
      return "Cannot create bridged guest network when host is using WiFi; "
             "disconnect WiFi and connect via ethernet";
  }
}

void GuestManager::Connect(fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller,
                           fuchsia::virtualization::GuestManager::ConnectCallback callback) {
  if (is_guest_started()) {
    context_->svc()->Connect(std::move(controller));
    callback(fpromise::ok());
  } else {
    FX_LOGS(ERROR) << "Failed to connect to guest. Guest is not running";
    callback(fpromise::error(GuestManagerError::NOT_RUNNING));
  }
}

std::vector<std::string> GuestManager::CheckForProblems() {
  std::vector<std::string> problems;

  GuestNetworkState network_state = QueryGuestNetworkState();
  switch (network_state) {
    case GuestNetworkState::OK:
    case GuestNetworkState::NO_NETWORK_DEVICE:
      // Do nothing.
      break;
    case GuestNetworkState::FAILED_TO_QUERY:
    case GuestNetworkState::NO_HOST_NETWORKING:
    case GuestNetworkState::MISSING_VIRTUAL_INTERFACES:
    case GuestNetworkState::NO_BRIDGE_CREATED:
    case GuestNetworkState::ATTEMPTED_TO_BRIDGE_WITH_WLAN:
      problems.push_back(GuestNetworkStateToStringExplanation(network_state));
      break;
  }

  if (memory_pressure_handler_ != nullptr) {
    switch (memory_pressure_handler_->get_latest_memory_pressure_event()) {
      case fuchsia_memorypressure::Level::kNormal:
        // Do nothing.
        break;
      case fuchsia_memorypressure::Level::kWarning:
        problems.push_back("Host is experiencing moderate memory pressure");
        break;
      case fuchsia_memorypressure::Level::kCritical:
        problems.push_back("Host is experiencing severe memory pressure");
        break;
    }
  }

  return problems;
}

void GuestManager::GetInfo(GetInfoCallback callback) {
  fuchsia::virtualization::GuestInfo info;
  info.set_guest_status(state_);

  switch (state_) {
    case GuestStatus::STARTING:
    case GuestStatus::RUNNING:
    case GuestStatus::STOPPING: {
      GuestDescriptor descriptor;
      FX_CHECK(guest_descriptor_.Clone(&descriptor) == ZX_OK);
      info.set_guest_descriptor(std::move(descriptor));
      info.set_uptime((zx::clock::get_monotonic() - start_time_).to_nsecs());
      break;
    }
    case GuestStatus::STOPPED: {
      info.set_uptime((stop_time_ - start_time_).to_nsecs());
      if (last_error_.has_value()) {
        info.set_stop_error(last_error_.value());
      }
      break;
    }
    case GuestStatus::VMM_UNEXPECTED_TERMINATION:
    case GuestStatus::NOT_STARTED:
      // Do nothing.
      break;
  }

  *info.mutable_detected_problems() = CheckForProblems();

  callback(std::move(info));
}

void GuestManager::SnapshotConfig(const fuchsia::virtualization::GuestConfig& config) {
  guest_descriptor_.set_num_cpus(config.cpus());
  guest_descriptor_.set_guest_memory(config.guest_memory());

  guest_descriptor_.set_wayland(config.has_wayland_device());
  guest_descriptor_.set_magma(config.has_magma_device());

  guest_descriptor_.set_balloon(config.has_virtio_balloon() && config.virtio_balloon());
  guest_descriptor_.set_console(config.has_virtio_console() && config.virtio_console());
  guest_descriptor_.set_gpu(config.has_virtio_gpu() && config.virtio_gpu());
  guest_descriptor_.set_rng(config.has_virtio_rng() && config.virtio_rng());
  guest_descriptor_.set_vsock(config.has_virtio_vsock() && config.virtio_vsock());
  guest_descriptor_.set_sound(config.has_virtio_sound() && config.virtio_sound());

  if (config.has_net_devices()) {
    *guest_descriptor_.mutable_networks() = config.net_devices();
  }
}

bool GuestManager::is_guest_started() const {
  switch (state_) {
    case GuestStatus::STARTING:
    case GuestStatus::RUNNING:
    case GuestStatus::STOPPING:
      return true;
    default:
      return false;
  }
}
