// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_X64_H_
#define SRC_STORAGE_LIB_PAVER_X64_H_

#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/gpt.h"

namespace paver {

// DevicePartitioner implementation for EFI based devices.
class EfiDevicePartitioner : public DevicePartitioner {
 public:
  static zx::result<std::unique_ptr<DevicePartitioner>> Initialize(
      fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
      const fbl::unique_fd& block_device);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::result<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::result<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::result<> FinalizePartition(const PartitionSpec& spec) const override;

  zx::result<> WipeFvm() const override;

  zx::result<> InitPartitionTables() const override;

  zx::result<> WipePartitionTables() const override;

  zx::result<> ValidatePayload(const PartitionSpec& spec,
                               cpp20::span<const uint8_t> data) const override;

  zx::result<> Flush() const override { return zx::ok(); }

 private:
  EfiDevicePartitioner(Arch arch, std::unique_ptr<GptDevicePartitioner> gpt)
      : gpt_(std::move(gpt)), arch_(arch) {}

  std::unique_ptr<GptDevicePartitioner> gpt_;
  Arch arch_;
};

class X64PartitionerFactory : public DevicePartitionerFactory {
 public:
  zx::result<std::unique_ptr<DevicePartitioner>> New(
      fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
      std::shared_ptr<Context> context, const fbl::unique_fd& block_device) final;
};

class X64AbrClientFactory : public abr::ClientFactory {
 public:
  zx::result<std::unique_ptr<abr::Client>> New(
      fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
      std::shared_ptr<paver::Context> context) final;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_X64_H_
