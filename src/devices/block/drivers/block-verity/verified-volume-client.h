// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_VERIFIED_VOLUME_CLIENT_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_VERIFIED_VOLUME_CLIENT_H_

#include <fidl/fuchsia.hardware.block.verified/cpp/wire.h>
#include <lib/zx/channel.h>

#include <fbl/unique_fd.h>

#include "src/lib/digest/digest.h"

namespace block_verity {

// `VerifiedVolumeClient` is a client library to ease interacting with
// `fuchsia.hardware.block.verified` devices and their children.
class VerifiedVolumeClient {
 public:
  VerifiedVolumeClient(zx::channel verity_chan, fbl::unique_fd devfs_root_fd);

  // Disallow copy, assign, and move
  VerifiedVolumeClient(const VerifiedVolumeClient&) = delete;
  VerifiedVolumeClient& operator=(const VerifiedVolumeClient&) = delete;
  VerifiedVolumeClient(VerifiedVolumeClient&&) = delete;
  VerifiedVolumeClient& operator=(VerifiedVolumeClient&&) = delete;

  enum Disposition {
    kDriverAlreadyBound,
    kDriverNeedsBinding,
  };

  // Given a borrowed fd to a block device (`block_dev_fd`) and an owned fd for
  // the the devfs root (`devfs_root_fd`), prepare a `VerifiedVolumeClient` by
  // possibly binding the driver according to `disposition` and waiting up to
  // `timeout` for the `verity` child of `block_dev_fd` to appear.
  static zx_status_t CreateFromBlockDevice(int block_dev_fd, fbl::unique_fd devfs_root_fd,
                                           Disposition disposition, const zx::duration& timeout,
                                           std::unique_ptr<VerifiedVolumeClient>* out);

  // Requests that the volume be opened for authoring.  If successful,
  // `mutable_block_fd_out` will contain an open handle to the mutable block
  // device.
  zx_status_t OpenForAuthoring(const zx::duration& timeout, fbl::unique_fd& mutable_block_fd_out);

  // Requests that any child device (mutable or verified) created by
  // `OpenForAuthoring` or `OpenForVerifiedRead` be unbound.
  zx_status_t Close();

  // Requests that the volume unbind the `mutable` child, regenerated integrity
  // data, update the superblock, and return a seal for future use with
  // `OpenForVerifiedRead`.  If successful, the result of the seal operation is
  // written into the caller-owned `out` buffer, so the caller can persist it
  // somewhere.
  zx_status_t CloseAndGenerateSeal(
      fidl::Buffer<
          fidl::WireResponse<fuchsia_hardware_block_verified::DeviceManager::CloseAndGenerateSeal>>*
          seal_response_buffer,
      fuchsia_hardware_block_verified::wire::DeviceManagerCloseAndGenerateSealResult* out);

  // Requests that the volume be opened for verified reads, with the expectation
  // that the volume superblock matches the seal provided.  If successful,
  // `verified_block_fd_out` will contain a handle to the verified block device.
  zx_status_t OpenForVerifiedRead(const digest::Digest& expected_seal, const zx::duration& timeout,
                                  fbl::unique_fd& verified_block_fd_out);

 private:
  zx::channel verity_chan_;
  fbl::unique_fd devfs_root_fd_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_VERIFIED_VOLUME_CLIENT_H_
