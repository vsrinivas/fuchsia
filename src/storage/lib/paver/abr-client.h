// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_ABR_CLIENT_H_
#define SRC_STORAGE_LIB_PAVER_ABR_CLIENT_H_

#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/abr/abr.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <memory>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/storage/lib/paver/partition-client.h"
#include "src/storage/lib/paver/paver-context.h"

namespace abr {

zx::status<llcpp::fuchsia::paver::Configuration> QueryBootConfig(const zx::channel& svc_root);

// Interface for interacting with ABR data.
class Client {
 public:
  // Factory create method.
  static zx::status<std::unique_ptr<abr::Client>> Create(fbl::unique_fd devfs_root,
                                                         const zx::channel& svc_root,
                                                         std::shared_ptr<paver::Context> context);
  virtual ~Client() = default;

  AbrSlotIndex GetBootSlot(bool update_metadata, bool* is_slot_marked_successful) const {
    return AbrGetBootSlot(&abr_ops_, update_metadata, is_slot_marked_successful);
  }

  zx::status<> MarkSlotActive(AbrSlotIndex index) {
    return AbrResultToZxStatus(AbrMarkSlotActive(&abr_ops_, index));
  }

  zx::status<> MarkSlotUnbootable(AbrSlotIndex index) {
    return AbrResultToZxStatus(AbrMarkSlotUnbootable(&abr_ops_, index));
  }

  zx::status<> MarkSlotSuccessful(AbrSlotIndex index) {
    return AbrResultToZxStatus(AbrMarkSlotSuccessful(&abr_ops_, index));
  }

  zx::status<AbrSlotInfo> GetSlotInfo(AbrSlotIndex index) const {
    AbrSlotInfo info;
    auto status = AbrResultToZxStatus(AbrGetSlotInfo(&abr_ops_, index, &info));
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(info);
  }

  static zx::status<> AbrResultToZxStatus(AbrResult status);

  virtual zx::status<> Flush() const = 0;

  void InitializeAbrOps();

  Client() { abr_ops_ = {this, Client::ReadAbrMetaData, Client::WriteAbrMetaData}; }

  // No copy, move, assign.
  // This is to ensure that |abr_ops_| is always valid. |abr_ops_.context| shall always be
  // a |this| pointer to the Client instance that hosts |abr_ops_|. This may be
  // violated if we allow Client to be copied/moved/assign.
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;

 private:
  AbrOps abr_ops_;

  // ReadAbrMetaData and WriteAbrMetaData will be assigned to fields in AbrOps
  static bool ReadAbrMetaData(void* context, size_t size, uint8_t* buffer);

  static bool WriteAbrMetaData(void* context, const uint8_t* buffer, size_t size);

  virtual zx::status<> Read(uint8_t* buffer, size_t size) = 0;

  virtual zx::status<> Write(const uint8_t* buffer, size_t size) = 0;
};

class ClientFactory {
 public:
  // Factory create method.
  static zx::status<std::unique_ptr<abr::Client>> Create(fbl::unique_fd devfs_root,
                                                         const zx::channel& svc_root,
                                                         std::shared_ptr<paver::Context> context);

  static void Register(std::unique_ptr<ClientFactory> factory);

  virtual ~ClientFactory() = default;

 private:
  virtual zx::status<std::unique_ptr<abr::Client>> New(fbl::unique_fd devfs_root,
                                                       const zx::channel& svc_root,
                                                       std::shared_ptr<paver::Context> context) = 0;

  static std::vector<std::unique_ptr<ClientFactory>>* registered_factory_list();
};

// Implementation of abr::Client which works with a contiguous partition storing AbrData.
class AbrPartitionClient : public Client {
 public:
  // |partition| should contain AbrData with no offset.
  static zx::status<std::unique_ptr<abr::Client>> Create(
      std::unique_ptr<paver::PartitionClient> partition);

 private:
  AbrPartitionClient(std::unique_ptr<paver::PartitionClient> partition, zx::vmo vmo,
                     size_t block_size)
      : partition_(std::move(partition)), vmo_(std::move(vmo)), block_size_(block_size) {}

  zx::status<> Read(uint8_t* buffer, size_t size) override;

  zx::status<> Write(const uint8_t* buffer, size_t size) override;

  zx::status<> Flush() const override { return partition_->Flush(); }

  std::unique_ptr<paver::PartitionClient> partition_;
  zx::vmo vmo_;
  size_t block_size_;
};

}  // namespace abr

#endif  // SRC_STORAGE_LIB_PAVER_ABR_CLIENT_H_
