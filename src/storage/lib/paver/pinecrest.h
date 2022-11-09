// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_PINECREST_H_
#define SRC_STORAGE_LIB_PAVER_PINECREST_H_

#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/gpt.h"
#include "src/storage/lib/paver/partition-client.h"

namespace paver {

class PinecrestPartitioner : public DevicePartitioner {
 public:
  static zx::result<std::unique_ptr<DevicePartitioner>> Initialize(
      fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
      const fbl::unique_fd& block_device);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::result<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::result<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::result<> FinalizePartition(const PartitionSpec& spec) const override { return zx::ok(); }

  zx::result<> WipeFvm() const override;

  zx::result<> InitPartitionTables() const override;

  zx::result<> WipePartitionTables() const override;

  zx::result<> ValidatePayload(const PartitionSpec& spec,
                               cpp20::span<const uint8_t> data) const override;

  zx::result<> Flush() const override { return zx::ok(); }

 private:
  explicit PinecrestPartitioner(std::unique_ptr<GptDevicePartitioner> gpt) : gpt_(std::move(gpt)) {}

  zx::result<std::unique_ptr<PartitionClient>> FindPartitionByGuid(const PartitionSpec& spec) const;
  zx::result<std::unique_ptr<PartitionClient>> FindPartitionByName(const PartitionSpec& spec) const;

  std::unique_ptr<GptDevicePartitioner> gpt_;
};

class PinecrestPartitionerFactory : public DevicePartitionerFactory {
 public:
  zx::result<std::unique_ptr<DevicePartitioner>> New(
      fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
      std::shared_ptr<Context> context, const fbl::unique_fd& block_device) final;
};

class PinecrestAbrClientFactory : public abr::ClientFactory {
 public:
  zx::result<std::unique_ptr<abr::Client>> New(
      fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
      std::shared_ptr<paver::Context> context) final;
};

class PinecrestAbrClient : public PartitionClient {
 public:
  PinecrestAbrClient(std::unique_ptr<paver::PartitionClient> client, AbrSlotIndex firmware_slot)
      : client_(std::move(client)), firmware_slot_(firmware_slot) {}

  zx::result<size_t> GetBlockSize() override { return client_->GetBlockSize(); }
  zx::result<size_t> GetPartitionSize() override { return client_->GetPartitionSize(); }
  zx::result<> Trim() override { return client_->Trim(); }
  zx::result<> Flush() override { return client_->Flush(); }
  fbl::unique_fd block_fd() override { return client_->block_fd(); }
  zx::result<> Read(const zx::vmo& vmo, size_t size) override;
  zx::result<> Write(const zx::vmo& vmo, size_t vmo_size) override;

 private:
  std::unique_ptr<paver::PartitionClient> client_;
  AbrSlotIndex firmware_slot_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_PINECREST_H_
