// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_manager.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_set>

#include <src/lib/files/file.h>

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
    : context_(context),
      config_pkg_dir_path_(std::move(config_pkg_dir_path)),
      config_path_(std::move(config_path)) {
  context_->outgoing()->AddPublicService(manager_bindings_.GetHandler(this));
}

fitx::result<GuestManagerError, GuestConfig> GuestManager::GetDefaultGuestConfig() {
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
    return fitx::error(GuestManagerError::BAD_CONFIG);
  }
  auto config = guest_config::ParseConfig(content, std::move(open_at));
  if (config.is_error()) {
    FX_PLOGS(ERROR, config.error_value()) << "Failed to parse guest configuration " << config_path;
    return fitx::error(GuestManagerError::BAD_CONFIG);
  }

  return fitx::ok(std::move(*config));
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
    lifecycle_.set_error_handler(
        [this](zx_status_t) { state_ = GuestStatus::VMM_UNEXPECTED_TERMINATION; });
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
        // TODO(https://fxbug.dev/67566): Enable once bridging is fixed.
        .enable_bridge = false,
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

  lifecycle_->Create(std::move(merged_cfg), [this, controller = std::move(controller),
                                             callback = std::move(callback)](
                                                GuestLifecycle_Create_Result result) mutable {
    this->HandleCreateResult(std::move(result), std::move(controller), std::move(callback));
  });
}

void GuestManager::HandleCreateResult(
    GuestLifecycle_Create_Result result,
    fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller, LaunchCallback callback) {
  if (result.is_err()) {
    HandleGuestStopped(fitx::error(result.err()));
    callback(fpromise::error(GuestManagerError::START_FAILURE));
  } else {
    state_ = GuestStatus::RUNNING;
    lifecycle_->Run(
        [this](GuestLifecycle_Run_Result result) { this->HandleRunResult(std::move(result)); });
    context_->svc()->Connect(std::move(controller));
    OnGuestLaunched();
    callback(fpromise::ok());
  }
}

void GuestManager::HandleRunResult(GuestLifecycle_Run_Result result) {
  if (result.is_response()) {
    HandleGuestStopped(fitx::ok());
  } else {
    HandleGuestStopped(fitx::error(result.err()));
  }
}

void GuestManager::HandleGuestStopped(fitx::result<GuestError> err) {
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
    case GuestStatus::NOT_STARTED: {
      // Do nothing.
      break;
    }
  }

  callback(std::move(info));
}

void GuestManager::SnapshotConfig(const fuchsia::virtualization::GuestConfig& config) {
  guest_descriptor_.set_num_cpus(config.cpus());
  guest_descriptor_.set_guest_memory(config.guest_memory());

  guest_descriptor_.set_wayland(config.has_wayland_device());
  guest_descriptor_.set_magma(config.has_magma_device());

  guest_descriptor_.set_network(config.has_default_net() && config.default_net());
  guest_descriptor_.set_balloon(config.has_virtio_balloon() && config.virtio_balloon());
  guest_descriptor_.set_console(config.has_virtio_console() && config.virtio_console());
  guest_descriptor_.set_gpu(config.has_virtio_gpu() && config.virtio_gpu());
  guest_descriptor_.set_rng(config.has_virtio_rng() && config.virtio_rng());
  guest_descriptor_.set_vsock(config.has_virtio_vsock() && config.virtio_vsock());
  guest_descriptor_.set_sound(config.has_virtio_sound() && config.virtio_sound());
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
