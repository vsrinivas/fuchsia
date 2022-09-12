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

using ::fuchsia::virtualization::GuestDescriptor;
using ::fuchsia::virtualization::GuestStatus;

GuestManager::GuestManager(async_dispatcher_t* dispatcher, sys::ComponentContext* context,
                           std::string config_pkg_dir_path, std::string config_path)
    : context_(context),
      config_pkg_dir_path_(std::move(config_pkg_dir_path)),
      config_path_(std::move(config_path)) {
  context_->outgoing()->AddPublicService(manager_bindings_.GetHandler(this));
  context_->outgoing()->AddPublicService(guest_config_bindings_.GetHandler(this));
}

zx::status<fuchsia::virtualization::GuestConfig> GuestManager::GetDefaultGuestConfig() {
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
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  auto config = guest_config::ParseConfig(content, std::move(open_at));
  if (config.is_error()) {
    FX_PLOGS(ERROR, config.error_value()) << "Failed to parse guest configuration " << config_path;
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok(std::move(*config));
}

// |fuchsia::virtualization::GuestManager|
void GuestManager::LaunchGuest(
    fuchsia::virtualization::GuestConfig user_config,
    fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller,
    fuchsia::virtualization::GuestManager::LaunchGuestCallback callback) {
  if (is_guest_started()) {
    callback(fpromise::error(ZX_ERR_ALREADY_EXISTS));
    return;
  }

  auto default_config = GetDefaultGuestConfig();
  if (default_config.is_error()) {
    callback(fpromise::error(default_config.error_value()));
    return;
  }

  // Use the static config as a base, but apply the user config as an override.
  guest_config_ = guest_config::MergeConfigs(std::move(*default_config), std::move(user_config));
  if (!guest_config_.has_guest_memory()) {
    guest_config_.set_guest_memory(GetDefaultGuestMemory());
  }
  if (!guest_config_.has_cpus()) {
    guest_config_.set_cpus(GetDefaultNumCpus());
  }

  if (guest_config_.has_default_net() && guest_config_.default_net()) {
    guest_config_.mutable_net_devices()->push_back({
        .mac_address = kGuestMacAddress,
        // TODO(https://fxbug.dev/67566): Enable once bridging is fixed.
        .enable_bridge = false,
    });
  }

  // Merge the command-line additions into the main kernel command-line field.
  for (auto& cmdline : *guest_config_.mutable_cmdline_add()) {
    guest_config_.mutable_cmdline()->append(" " + cmdline);
  }
  guest_config_.clear_cmdline_add();

  // If there are any initial vsock listeners, they must be bound to unique host ports.
  if (guest_config_.has_vsock_listeners() && guest_config_.vsock_listeners().size() > 1) {
    std::unordered_set<uint32_t> ports;
    for (auto& listener : guest_config_.vsock_listeners()) {
      if (!ports.insert(listener.port).second) {
        callback(fpromise::error(ZX_ERR_INVALID_ARGS));
        return;
      }
    }
  }

  // Connect call will cause componont framework to start VMM and execute VMM's main which will
  // bring up all virtio devices and query guest_config via the
  // fuchsia::virtualization::GuestConfigProvider::Get. Note that this must happen after the
  // config is finalized.

  // Once this VMM starts running there's no way to stop it, so for now the only state is RUNNING.
  // TODO(fxbug.dev/104989): Implement granular states once Stop/Start works.
  state_ = GuestStatus::RUNNING;

  start_time_ = zx::clock::get_monotonic();
  SnapshotConfig(guest_config_);
  context_->svc()->Connect(std::move(controller));

  OnGuestLaunched();
  callback(fpromise::ok());
}

void GuestManager::ConnectToGuest(
    fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller,
    fuchsia::virtualization::GuestManager::ConnectToGuestCallback callback) {
  if (is_guest_started()) {
    context_->svc()->Connect(std::move(controller));
    callback(fpromise::ok());
  } else {
    FX_LOGS(ERROR) << "Failed to connect to guest. Guest is not running";
    callback(fpromise::error(ZX_ERR_UNAVAILABLE));
  }
}

void GuestManager::GetGuestInfo(GetGuestInfoCallback callback) {
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

// |fuchsia::virtualization::GuestConfigProvider|
void GuestManager::Get(GetCallback callback) {
  // This function is called by VMM as part of its main() to configure itself
  //
  // GuestConfigProvider::Get is expected to be called only once per LaunchGuest
  // TODO(fxbug.dev/103621) Restructure VMM's Guest to have an explicit Start and Stop function.
  // This will remove the need for the fuchsia::virtualization::GuestConfigProvider.
  //  GuestManager::LaunchGuest could simply connect to VMM's Guest protocol and call Start
  callback(std::move(guest_config_));
}
