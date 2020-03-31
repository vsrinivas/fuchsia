// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_PAVER_ABR_CLIENT_H_
#define ZIRCON_SYSTEM_ULIB_PAVER_ABR_CLIENT_H_

#include <lib/zx/channel.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <libabr/libabr.h>

namespace abr {

// Interface for interacting with ABR data.
class Client {
 public:
  // Factory create method.
  static zx_status_t Create(fbl::unique_fd devfs_root, const zx::channel& svc_root,
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
  static zx_status_t Create(fbl::unique_fd devfs_root, std::unique_ptr<abr::Client>* out);
};

class SherlockClient {
 public:
  static zx_status_t Create(fbl::unique_fd devfs_root, std::unique_ptr<abr::Client>* out);
};

}  // namespace abr

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_ABR_CLIENT_H_
