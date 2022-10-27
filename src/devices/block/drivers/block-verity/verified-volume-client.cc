// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/verified-volume-client.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.verified/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>

#include <cstring>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <ramdevice-client/ramdisk.h>  // for wait_for_device_at

#include "lib/stdcompat/string_view.h"

namespace block_verity {
namespace {

const char* kDriverLib = "/boot/driver/block-verity.so";

zx_status_t BindVerityDriver(fidl::UnownedClientEnd<fuchsia_device::Controller> channel) {
  const fidl::WireResult result =
      fidl::WireCall(channel)->Bind(fidl::StringView::FromExternal(kDriverLib));
  if (!result.ok()) {
    return result.status();
  }
  const fit::result response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  return ZX_OK;
}

zx_status_t RelativeTopologicalPath(fidl::UnownedClientEnd<fuchsia_device::Controller> channel,
                                    fbl::String* out) {
  const fidl::WireResult result = fidl::WireCall(channel)->GetTopologicalPath();
  if (!result.ok()) {
    return result.status();
  }
  const fit::result response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  std::string_view path = response.value()->path.get();

  constexpr std::string_view kSlashDevSlash = "/dev/";
  if (!cpp20::starts_with(path, kSlashDevSlash)) {
    return ZX_ERR_INTERNAL;
  }
  *out = path.substr(kSlashDevSlash.size());
  return ZX_OK;
}

}  // namespace

VerifiedVolumeClient::VerifiedVolumeClient(
    fidl::ClientEnd<fuchsia_hardware_block_verified::DeviceManager> verity_chan,
    fbl::unique_fd devfs_root_fd)
    : verity_chan_(std::move(verity_chan)), devfs_root_fd_(std::move(devfs_root_fd)) {}

zx_status_t VerifiedVolumeClient::CreateFromBlockDevice(
    fidl::UnownedClientEnd<fuchsia_device::Controller> device, fbl::unique_fd devfs_root_fd,
    Disposition disposition, const zx::duration& timeout,
    std::unique_ptr<VerifiedVolumeClient>* out) {
  // Bind the driver if called for by `disposition`.
  if (disposition == kDriverNeedsBinding) {
    if (zx_status_t status = BindVerityDriver(device); status != ZX_OK) {
      printf("VerifiedVolumeClient: couldn't bind driver: %s", zx_status_get_string(status));
      return status;
    }
  }

  // Compute the path at which we expect to see the verity child device appear.
  fbl::String block_dev_path;
  if (zx_status_t status = RelativeTopologicalPath(device, &block_dev_path); status != ZX_OK) {
    printf("VerifiedVolumeClient: could not compute relative path: %s\n",
           zx_status_get_string(status));
    return status;
  }
  fbl::String verity_path = fbl::String::Concat({block_dev_path, "/verity"});

  // Wait for the device to appear.
  if (zx_status_t status =
          wait_for_device_at(devfs_root_fd.get(), verity_path.c_str(), timeout.get());
      status != ZX_OK) {
    printf("VerifiedVolumeClient: verity device failed to appear: %s\n",
           zx_status_get_string(status));
    return status;
  }

  // Open the device.
  fbl::unique_fd verity_fd(openat(devfs_root_fd.get(), verity_path.c_str(), O_RDWR));
  if (!verity_fd) {
    printf("VerifiedVolumeClient: couldn't open verity device at %s\n", verity_path.c_str());
    return ZX_ERR_NOT_FOUND;
  }

  // Take the channel from the FD.
  fdio_cpp::FdioCaller verity_caller(std::move(verity_fd));
  zx::result verity_chan = verity_caller.take_as<fuchsia_hardware_block_verified::DeviceManager>();
  if (verity_chan.is_error()) {
    printf("VerifiedVolumeClient: couldn't get verity channel from fd: %s\n",
           verity_chan.status_string());
    return verity_chan.status_value();
  }

  // Create client.
  std::unique_ptr<VerifiedVolumeClient> vvc = std::make_unique<VerifiedVolumeClient>(
      std::move(verity_chan.value()), std::move(devfs_root_fd));
  // Move to caller's buffer.
  *out = std::move(vvc);
  return ZX_OK;
}

zx_status_t VerifiedVolumeClient::OpenForAuthoring(const zx::duration& timeout,
                                                   fbl::unique_fd& mutable_block_fd_out) {
  // make FIDL call to open in authoring mode
  fidl::Arena allocator;

  // Request the device be opened for writes
  auto open_resp =
      fidl::WireCall(verity_chan_)
          ->OpenForWrite(
              fuchsia_hardware_block_verified::wire::Config::Builder(allocator)
                  .hash_function(fuchsia_hardware_block_verified::wire::HashFunction::kSha256)
                  .block_size(fuchsia_hardware_block_verified::wire::BlockSize::kSize4096)
                  .Build());
  if (open_resp.status() != ZX_OK) {
    return open_resp.status();
  }
  if (open_resp->is_error()) {
    return open_resp->error_value();
  }

  // Compute path of expected `mutable` child device via relative topological path
  fbl::String verity_path;
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  if (zx_status_t status = RelativeTopologicalPath(
          fidl::UnownedClientEnd<fuchsia_device::Controller>(verity_chan_.channel().borrow()),
          &verity_path);
      status != ZX_OK) {
    printf("VerifiedVolumeClient: could not compute relative path: %s\n",
           zx_status_get_string(status));
    return status;
  }
  fbl::String mutable_path = fbl::String::Concat({verity_path, "/mutable"});

  // Wait for the `mutable` child device to appear
  if (zx_status_t status =
          wait_for_device_at(devfs_root_fd_.get(), mutable_path.c_str(), timeout.get());
      status != ZX_OK) {
    printf("VerifiedVolumeClient: mutable device failed to appear: %s\n",
           zx_status_get_string(status));
    return status;
  }

  // Then wait for the `block` child of that mutable device
  fbl::String mutable_block_path = fbl::String::Concat({mutable_path, "/block"});
  if (zx_status_t status =
          wait_for_device_at(devfs_root_fd_.get(), mutable_block_path.c_str(), timeout.get());
      status != ZX_OK) {
    printf("VerifiedVolumeClient: mutable block device failed to appear: %s\n",
           zx_status_get_string(status));
    return status;
  }

  // Open child device and return
  mutable_block_fd_out.reset(openat(devfs_root_fd_.get(), mutable_block_path.c_str(), O_RDWR));
  if (!mutable_block_fd_out) {
    printf("VerifiedVolumeClient: failed to open %s\n", mutable_block_path.c_str());
    return ZX_ERR_NOT_FOUND;
  }

  return ZX_OK;
}

zx_status_t VerifiedVolumeClient::Close() {
  // Close the device cleanly
  auto close_resp = fidl::WireCall(verity_chan_)->Close();
  if (close_resp.status() != ZX_OK) {
    return close_resp.status();
  }
  if (close_resp->is_error()) {
    return close_resp->error_value();
  }

  return ZX_OK;
}

zx_status_t VerifiedVolumeClient::CloseAndGenerateSeal(
    fidl::AnyArena& arena,
    fuchsia_hardware_block_verified::wire::DeviceManagerCloseAndGenerateSealResponse* out) {
  // We use the caller-provided buffer FIDL call style because the caller
  // needs to do something with the seal returned, so we need to keep the
  // response object alive so that the caller can interact with it after this
  // function returns.
  auto seal_resp = fidl::WireCall(verity_chan_).buffer(arena)->CloseAndGenerateSeal();
  if (seal_resp.status() != ZX_OK) {
    return seal_resp.status();
  }
  if (seal_resp->is_error()) {
    return seal_resp->error_value();
  }

  *out = *seal_resp.value().value();
  return ZX_OK;
}

zx_status_t VerifiedVolumeClient::OpenForVerifiedRead(const digest::Digest& expected_seal,
                                                      const zx::duration& timeout,
                                                      fbl::unique_fd& verified_block_fd_out) {
  // make FIDL call to open in authoring mode
  fidl::Arena allocator;

  // Make a copy of the seal to send.
  fuchsia_hardware_block_verified::wire::Sha256Seal sha256_seal;
  expected_seal.CopyTo(sha256_seal.superblock_hash.begin(), sha256_seal.superblock_hash.size());

  // Request the device be opened for verified read
  auto open_resp =
      fidl::WireCall(verity_chan_)
          ->OpenForVerifiedRead(
              fuchsia_hardware_block_verified::wire::Config::Builder(allocator)
                  .hash_function(fuchsia_hardware_block_verified::wire::HashFunction::kSha256)
                  .block_size(fuchsia_hardware_block_verified::wire::BlockSize::kSize4096)
                  .Build(),
              fuchsia_hardware_block_verified::wire::Seal::WithSha256(
                  fidl::ObjectView<fuchsia_hardware_block_verified::wire::Sha256Seal>::FromExternal(
                      &sha256_seal)));
  if (open_resp.status() != ZX_OK) {
    return open_resp.status();
  }
  if (open_resp->is_error()) {
    return open_resp->error_value();
  }

  // Compute path of expected `verified` child device via relative topological path
  fbl::String verity_path;
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  if (zx_status_t status = RelativeTopologicalPath(
          fidl::UnownedClientEnd<fuchsia_device::Controller>(verity_chan_.channel().borrow()),
          &verity_path);
      status != ZX_OK) {
    printf("VerifiedVolumeClient: could not compute relative path: %s\n",
           zx_status_get_string(status));
    return status;
  }
  fbl::String verified_path = fbl::String::Concat({verity_path, "/verified"});

  // Wait for the `verified` child device to appear
  fbl::unique_fd verified_fd;
  if (zx_status_t status =
          wait_for_device_at(devfs_root_fd_.get(), verified_path.c_str(), timeout.get());
      status != ZX_OK) {
    printf("VerifiedVolumeClient: verified device failed to appear: %s\n",
           zx_status_get_string(status));
    return status;
  }

  // Then wait for the `block` child of that verified device
  fbl::String verified_block_path = fbl::String::Concat({verified_path, "/block"});
  if (zx_status_t status =
          wait_for_device_at(devfs_root_fd_.get(), verified_block_path.c_str(), timeout.get());
      status != ZX_OK) {
    printf("VerifiedVolumeClient: verified block device failed to appear: %s\n",
           zx_status_get_string(status));
    return status;
  }

  verified_block_fd_out.reset(openat(devfs_root_fd_.get(), verified_block_path.c_str(), O_RDWR));
  if (!verified_block_fd_out) {
    printf("VerifiedVolumeClient: failed to open %s\n", verified_block_path.c_str());
    return ZX_ERR_NOT_FOUND;
  }

  return ZX_OK;
}

}  // namespace block_verity
