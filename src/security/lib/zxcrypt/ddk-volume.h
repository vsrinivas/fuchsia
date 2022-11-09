// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_ZXCRYPT_DDK_VOLUME_H_
#define SRC_SECURITY_LIB_ZXCRYPT_DDK_VOLUME_H_

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <memory>

#include "src/security/lib/zxcrypt/volume.h"

namespace zxcrypt {

class DdkVolume final : public Volume {
 public:
  explicit DdkVolume(zx_device_t* dev);

  // Unlocks a zxcrypt volume on the block device described by |dev| using the |key| corresponding
  // to given key |slot|.
  static zx_status_t Unlock(zx_device_t* dev, const crypto::Secret& key, key_slot_t slot,
                            std::unique_ptr<DdkVolume>* out);

  // Opens a zxcrypt volume on the block device described by |dev|, but does not
  // do any operations involving a key.  This is to make it possible to call Shred()
  // without necessarily holding the key.
  static zx_status_t OpenOpaque(zx_device_t* dev, std::unique_ptr<DdkVolume>* out);

  // Uses the data key material to initialize |cipher| for the given |direction|.
  zx_status_t Bind(crypto::Cipher::Direction direction, crypto::Cipher* cipher) const;

 private:
  // Retrieve the block/FVM information and adjust it
  zx_status_t Init();

  // Opens a zxcrypt volume on the underlying block device using the |key|
  // corresponding to given key |slot|.
  zx_status_t Unlock(const crypto::Secret& key, key_slot_t slot);

  zx_status_t GetBlockInfo(BlockInfo* out);
  zx_status_t GetFvmSliceSize(uint64_t* out);
  zx_status_t DoBlockFvmVsliceQuery(uint64_t vslice_start, SliceRegion ranges[MAX_SLICE_REGIONS],
                                    uint64_t* slice_count);
  zx_status_t DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count);

  // Reads a block from the current offset on the underlying device.
  zx_status_t Read();

  // Writes a block to the current offset on the underlying device.
  zx_status_t Write();

  // Flushes all pending writes to the underlying device.
  zx_status_t Flush();

  // The underlying block device, accessed via DDK
  zx_device_t* dev_;
};

}  // namespace zxcrypt

#endif  // SRC_SECURITY_LIB_ZXCRYPT_DDK_VOLUME_H_
