// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_ABR_CLIENT_H_
#define SRC_STORAGE_LIB_PAVER_ABR_CLIENT_H_

#include <lib/zx/channel.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <libabr/libabr.h>

#include "partition-client.h"
#include "paver-context.h"

namespace abr {

// Interface for interacting with ABR data.
class Client {
 public:
  // Factory create method.
  static zx_status_t Create(fbl::unique_fd devfs_root, const zx::channel& svc_root,
                            std::shared_ptr<paver::Context> context,
                            std::unique_ptr<abr::Client>* out);
  virtual ~Client() = default;

  AbrSlotIndex GetBootSlot(bool update_metadata, bool* is_slot_marked_successful) const {
    return AbrGetBootSlot(&abr_ops_, update_metadata, is_slot_marked_successful);
  }

  zx_status_t MarkSlotActive(AbrSlotIndex index) {
    return AbrResultToZxStatus(AbrMarkSlotActive(&abr_ops_, index));
  }

  zx_status_t MarkSlotUnbootable(AbrSlotIndex index) {
    return AbrResultToZxStatus(AbrMarkSlotUnbootable(&abr_ops_, index));
  }

  zx_status_t MarkSlotSuccessful(AbrSlotIndex index) {
    return AbrResultToZxStatus(AbrMarkSlotSuccessful(&abr_ops_, index));
  }

  zx_status_t GetSlotInfo(AbrSlotIndex index, AbrSlotInfo* info) const {
    return AbrResultToZxStatus(AbrGetSlotInfo(&abr_ops_, index, info));
  }

  static zx_status_t AbrResultToZxStatus(AbrResult status);

  virtual zx_status_t Flush() const = 0;

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

  virtual zx_status_t Read(uint8_t* buffer, size_t size) = 0;

  virtual zx_status_t Write(const uint8_t* buffer, size_t size) = 0;
};

class AstroClient {
 public:
  static zx_status_t Create(fbl::unique_fd devfs_root, const zx::channel& svc_root,
                            std::shared_ptr<paver::Context> context,
                            std::unique_ptr<abr::Client>* out);
};

class SherlockClient {
 public:
  static zx_status_t Create(fbl::unique_fd devfs_root, const zx::channel& svc_root,
                            std::unique_ptr<abr::Client>* out);
};

// Implementation of abr::Client which works with a contiguous partition storing AbrData.
class AbrPartitionClient : public Client {
 public:
  // |partition| should contain AbrData with no offset.
  static zx_status_t Create(std::unique_ptr<paver::PartitionClient> partition,
                            std::unique_ptr<abr::Client>* out);

 private:
  AbrPartitionClient(std::unique_ptr<paver::PartitionClient> partition, zx::vmo vmo,
                     size_t block_size)
      : partition_(std::move(partition)), vmo_(std::move(vmo)), block_size_(block_size) {}

  std::unique_ptr<paver::PartitionClient> partition_;
  zx::vmo vmo_;
  size_t block_size_;

  zx_status_t Read(uint8_t* buffer, size_t size) override;

  zx_status_t Write(const uint8_t* buffer, size_t size) override;

  zx_status_t Flush() const override { return partition_->Flush(); }
};

}  // namespace abr

#endif  // SRC_STORAGE_LIB_PAVER_ABR_CLIENT_H_
