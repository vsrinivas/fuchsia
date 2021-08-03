// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/verified-volume-client.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/verified/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>

#include <cstring>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <ramdevice-client/ramdisk.h>  // for wait_for_device_at

namespace block_verity {
namespace {

const char* kDriverLib = "/boot/driver/block-verity.so";

zx_status_t BindVerityDriver(zx::unowned_channel block_dev_chan) {
  zx_status_t rc;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(std::move(block_dev_chan))
                  .Bind(::fidl::StringView::FromExternal(kDriverLib));
  rc = resp.status();
  if (rc == ZX_OK) {
    if (resp->result.is_err()) {
      rc = resp->result.err();
    }
  }
  return rc;
}

zx_status_t RelativeTopologicalPath(zx::unowned_channel channel, fbl::String* out) {
  zx_status_t rc;

  // Get the full device path
  fbl::StringBuffer<PATH_MAX> path;
  path.Resize(path.capacity());
  size_t path_len;
  auto resp =
      fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(channel)).GetTopologicalPath();
  rc = resp.status();
  if (rc == ZX_OK) {
    if (resp->result.is_err()) {
      rc = resp->result.err();
    } else {
      auto& r = resp->result.response();
      path_len = r.path.size();
      memcpy(path.data(), r.path.data(), r.path.size());
    }
  }

  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: could not find parent device: %s\n", zx_status_get_string(rc));
    return rc;
  }

  // Verify that the path returned starts with "/dev/"
  const char* kSlashDevSlash = "/dev/";
  if (path_len < strlen(kSlashDevSlash)) {
    printf("VerifiedVolumeClient: path_len way too short: %lu\n", path_len);
    return ZX_ERR_INTERNAL;
  }
  if (strncmp(path.c_str(), kSlashDevSlash, strlen(kSlashDevSlash)) != 0) {
    printf("VerifiedVolumeClient: Expected device path to start with '/dev/' but got %s\n",
           path.c_str());
    return ZX_ERR_INTERNAL;
  }

  // Strip the leading "/dev/" and return the rest
  size_t path_len_sans_dev = path_len - strlen(kSlashDevSlash);
  memmove(path.begin(), path.begin() + strlen(kSlashDevSlash), path_len_sans_dev);

  path.Resize(path_len_sans_dev);
  *out = path.ToString();
  return ZX_OK;
}

}  // namespace

VerifiedVolumeClient::VerifiedVolumeClient(zx::channel verity_chan, fbl::unique_fd devfs_root_fd)
    : verity_chan_(std::move(verity_chan)), devfs_root_fd_(std::move(devfs_root_fd)) {}

zx_status_t VerifiedVolumeClient::CreateFromBlockDevice(
    int block_dev_fd, fbl::unique_fd devfs_root_fd, Disposition disposition,
    const zx::duration& timeout, std::unique_ptr<VerifiedVolumeClient>* out) {
  zx_status_t rc;

  // Bind the driver if called for by `disposition`.
  fdio_cpp::UnownedFdioCaller caller(block_dev_fd);
  if (disposition == kDriverNeedsBinding) {
    rc = BindVerityDriver(zx::unowned_channel(caller.borrow_channel()));
    if (rc != ZX_OK) {
      printf("VerifiedVolumeClient: couldn't bind driver: %s", zx_status_get_string(rc));
      return rc;
    }
  }

  // Compute the path at which we expect to see the verity child device appear.
  fbl::String block_dev_path;
  rc = RelativeTopologicalPath(zx::unowned_channel(caller.borrow_channel()), &block_dev_path);
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: could not compute relative path: %s\n", zx_status_get_string(rc));
    return rc;
  }
  fbl::String verity_path = fbl::String::Concat({block_dev_path, "/verity"});

  // Wait for the device to appear.
  rc = wait_for_device_at(devfs_root_fd.get(), verity_path.c_str(), timeout.get());
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: verity device failed to appear: %s\n", zx_status_get_string(rc));
    return rc;
  }

  // Open the device.
  fbl::unique_fd verity_fd(openat(devfs_root_fd.get(), verity_path.c_str(), O_RDWR));
  if (!verity_fd) {
    printf("VerifiedVolumeClient: couldn't open verity device at %s\n", verity_path.c_str());
    return ZX_ERR_NOT_FOUND;
  }

  // Take the channel from the FD.
  zx::channel verity_chan;
  rc = fdio_get_service_handle(verity_fd.release(), verity_chan.reset_and_get_address());
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: couldn't get verity channel from fd: %s\n",
           zx_status_get_string(rc));
    return rc;
  }

  // Create client.
  std::unique_ptr<VerifiedVolumeClient> vvc =
      std::make_unique<VerifiedVolumeClient>(std::move(verity_chan), std::move(devfs_root_fd));
  // Move to caller's buffer.
  *out = std::move(vvc);
  return ZX_OK;
}

zx_status_t VerifiedVolumeClient::OpenForAuthoring(const zx::duration& timeout,
                                                   fbl::unique_fd& mutable_block_fd_out) {
  // make FIDL call to open in authoring mode
  fuchsia_hardware_block_verified::wire::HashFunction hash_function =
      fuchsia_hardware_block_verified::wire::HashFunction::kSha256;
  fuchsia_hardware_block_verified::wire::BlockSize block_size =
      fuchsia_hardware_block_verified::wire::BlockSize::kSize4096;
  fidl::Arena allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(
      fidl::ObjectView<fuchsia_hardware_block_verified::wire::HashFunction>::FromExternal(
          &hash_function));
  config.set_block_size(
      fidl::ObjectView<fuchsia_hardware_block_verified::wire::BlockSize>::FromExternal(
          &block_size));

  // Request the device be opened for writes
  auto open_resp = fidl::WireCall<fuchsia_hardware_block_verified::DeviceManager>(
                       zx::unowned_channel(verity_chan_))
                       .OpenForWrite(std::move(config));
  if (open_resp.status() != ZX_OK) {
    return open_resp.status();
  }
  if (open_resp->result.is_err()) {
    return open_resp->result.err();
  }

  // Compute path of expected `mutable` child device via relative topological path
  fbl::String verity_path;
  zx_status_t rc;
  rc = RelativeTopologicalPath(zx::unowned_channel(verity_chan_), &verity_path);
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: could not compute relative path: %s\n", zx_status_get_string(rc));
    return rc;
  }
  fbl::String mutable_path = fbl::String::Concat({verity_path, "/mutable"});

  // Wait for the `mutable` child device to appear
  rc = wait_for_device_at(devfs_root_fd_.get(), mutable_path.c_str(), timeout.get());
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: mutable device failed to appear: %s\n", zx_status_get_string(rc));
    return rc;
  }

  // Then wait for the `block` child of that mutable device
  fbl::String mutable_block_path = fbl::String::Concat({mutable_path, "/block"});
  rc = wait_for_device_at(devfs_root_fd_.get(), mutable_block_path.c_str(), timeout.get());
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: mutable block device failed to appear: %s\n",
           zx_status_get_string(rc));
    return rc;
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
  auto close_resp = fidl::WireCall<fuchsia_hardware_block_verified::DeviceManager>(
                        zx::unowned_channel(verity_chan_))
                        .Close();
  if (close_resp.status() != ZX_OK) {
    return close_resp.status();
  }
  if (close_resp->result.is_err()) {
    return close_resp->result.err();
  }

  return ZX_OK;
}

zx_status_t VerifiedVolumeClient::CloseAndGenerateSeal(
    fidl::Buffer<
        fidl::WireResponse<fuchsia_hardware_block_verified::DeviceManager::CloseAndGenerateSeal>>*
        seal_response_buffer,
    fuchsia_hardware_block_verified::wire::DeviceManagerCloseAndGenerateSealResult* out) {
  // We use the caller-provided buffer FIDL call style because the caller
  // needs to do something with the seal returned, so we need to keep the
  // response object alive so that the caller can interact with it after this
  // function returns.
  auto seal_resp = fidl::WireCall<fuchsia_hardware_block_verified::DeviceManager>(
                       zx::unowned_channel(verity_chan_))
                       .CloseAndGenerateSeal(seal_response_buffer->view());
  if (seal_resp.status() != ZX_OK) {
    return seal_resp.status();
  }
  if (seal_resp->result.is_err()) {
    return seal_resp->result.err();
  }

  *out = std::move(seal_resp->result);
  return ZX_OK;
}

zx_status_t VerifiedVolumeClient::OpenForVerifiedRead(const digest::Digest& expected_seal,
                                                      const zx::duration& timeout,
                                                      fbl::unique_fd& verified_block_fd_out) {
  // make FIDL call to open in authoring mode
  fuchsia_hardware_block_verified::wire::HashFunction hash_function =
      fuchsia_hardware_block_verified::wire::HashFunction::kSha256;
  fuchsia_hardware_block_verified::wire::BlockSize block_size =
      fuchsia_hardware_block_verified::wire::BlockSize::kSize4096;
  fidl::Arena allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(
      fidl::ObjectView<fuchsia_hardware_block_verified::wire::HashFunction>::FromExternal(
          &hash_function));
  config.set_block_size(
      fidl::ObjectView<fuchsia_hardware_block_verified::wire::BlockSize>::FromExternal(
          &block_size));

  // Make a copy of the seal to send.
  fuchsia_hardware_block_verified::wire::Sha256Seal sha256_seal;
  expected_seal.CopyTo(sha256_seal.superblock_hash.begin(), sha256_seal.superblock_hash.size());
  auto seal_to_send = fuchsia_hardware_block_verified::wire::Seal::WithSha256(
      fidl::ObjectView<fuchsia_hardware_block_verified::wire::Sha256Seal>::FromExternal(
          &sha256_seal));

  // Request the device be opened for verified read
  auto open_resp = fidl::WireCall<fuchsia_hardware_block_verified::DeviceManager>(
                       zx::unowned_channel(verity_chan_))
                       .OpenForVerifiedRead(std::move(config), std::move(seal_to_send));
  if (open_resp.status() != ZX_OK) {
    return open_resp.status();
  }
  if (open_resp->result.is_err()) {
    return open_resp->result.err();
  }

  // Compute path of expected `verified` child device via relative topological path
  fbl::String verity_path;
  zx_status_t rc;
  rc = RelativeTopologicalPath(zx::unowned_channel(verity_chan_), &verity_path);
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: could not compute relative path: %s\n", zx_status_get_string(rc));
    return rc;
  }
  fbl::String verified_path = fbl::String::Concat({verity_path, "/verified"});

  // Wait for the `verified` child device to appear
  fbl::unique_fd verified_fd;
  rc = wait_for_device_at(devfs_root_fd_.get(), verified_path.c_str(), timeout.get());
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: verified device failed to appear: %s\n",
           zx_status_get_string(rc));
    return rc;
  }

  // Then wait for the `block` child of that verified device
  fbl::String verified_block_path = fbl::String::Concat({verified_path, "/block"});
  rc = wait_for_device_at(devfs_root_fd_.get(), verified_block_path.c_str(), timeout.get());
  if (rc != ZX_OK) {
    printf("VerifiedVolumeClient: verified block device failed to appear: %s\n",
           zx_status_get_string(rc));
    return rc;
  }

  verified_block_fd_out.reset(openat(devfs_root_fd_.get(), verified_block_path.c_str(), O_RDWR));
  if (!verified_block_fd_out) {
    printf("VerifiedVolumeClient: failed to open %s\n", verified_block_path.c_str());
    return ZX_ERR_NOT_FOUND;
  }

  return ZX_OK;
}

}  // namespace block_verity
