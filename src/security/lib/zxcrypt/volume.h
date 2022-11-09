// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_ZXCRYPT_VOLUME_H_
#define SRC_SECURITY_LIB_ZXCRYPT_VOLUME_H_

#include <lib/zx/time.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include "src/security/lib/fcrypto/aead.h"
#include "src/security/lib/fcrypto/bytes.h"
#include "src/security/lib/fcrypto/cipher.h"
#include "src/security/lib/fcrypto/digest.h"
#include "src/security/lib/fcrypto/secret.h"

// |zxcrypt::Volume| manages the interactions of both driver and library code with the metadata
// used to format and operate zxcrypt devices.  The superblock is saved multiple times on disk to
// provide redundancy.
//
// It manages three types of key material:
//  - Root: Provided by the consumers of this class.
//  - Data: Randomly generated at volume creation and used to encrypt and decrypt the volumes data.
//  - Wrap: Derived from the root keys and used to encrypt and decrypt the data key material.
//
// This is an abstract class, with concrete children for FD-backed and
// zx_device_t-backed implementations.
namespace zxcrypt {

// Forward declaration of unit test framework.
namespace testing {
class TestDevice;
}

const uint8_t zxcrypt_magic[16] = {
    0x5f, 0xe8, 0xf8, 0x00, 0xb3, 0x6d, 0x11, 0xe7, 0x80, 0x7a, 0x78, 0x63, 0x72, 0x79, 0x70, 0x74,
};

using key_slot_t = zx_off_t;

// Unifying types used for a couple of calls that will call either FIDL or Banjo
// interfaces under the hood.
typedef struct {
  uint64_t block_count;
  uint32_t block_size;
} BlockInfo;

typedef struct {
  bool allocated;
  size_t count;
} SliceRegion;

class __EXPORT Volume {
 public:
  // The supported version, named by the algorithms they use.  New version should increment the
  // version number and update the default version.  Zero indicates an error state.
  enum Version : uint32_t {
    kUninitialized = 0,
    kAES256_XTS_SHA256,
  };

  // The default version, used when sealing a new volume.
  static const Version kDefaultVersion;

  // The amount of data that can "in-flight" to the underlying block device before the zxcrypt
  // driver begins queuing transactions
  static const uint32_t kBufferSize;

  explicit Volume();
  virtual ~Volume();
  DISALLOW_COPY_ASSIGN_AND_MOVE(Volume);

  // Returns space reserved for metadata and keys
  size_t reserved_blocks() const { return reserved_blocks_; }
  size_t reserved_slices() const { return reserved_slices_; }
  size_t num_slots() const { return num_key_slots_; }

  zx_status_t Format(const crypto::Secret& key, key_slot_t slot);

  // Opens a zxcrypt volume on the block device described by |fd| using the |key| corresponding to
  // given key |slot|.
  zx_status_t Unlock(const crypto::Secret& key, key_slot_t slot);

  // Removes ALL keys, rendering any data in the zxcrypt device inaccessible.  It is an error to
  // call any method except the destructor on this instance after this methods returns.
  zx_status_t Shred();

 protected:
  friend class testing::TestDevice;

  ////////////////
  // Configuration methods

  // Retrieves the block and FVM information and adjusts it.
  zx_status_t Init();

  // Maps the volume version to crypto algorithms.
  zx_status_t Configure(Version version);

  // Returns via |out| the offset in bytes for the given key |slot|.  Returns an error if the
  // volume hasn't been initialized, or if |slot| is out of range.
  zx_status_t GetSlotOffset(key_slot_t slot, zx_off_t* out) const;

  // Derives intermediate keys for the given key |slot| from the given |key|.
  zx_status_t DeriveSlotKeys(const crypto::Secret& key, key_slot_t slot);

  // Resets all fields in this object to initial values
  void Reset();

  ////////////////
  // Block methods

  // Resets the superblock offset to the first location (block 0).
  zx_status_t Begin();

  // Advances the superblock offset to the next volume location.  Returns ZX_ERR_STOP if no more
  // offsets available; ZX_ERR_NEXT otherwise.
  zx_status_t Next();

  // Creates a new volume, with a new instance GUID and data key and IV, and seals it with the
  // given |key|
  zx_status_t CreateBlock();

  // Writes |block| out to each of the superblock locations.
  zx_status_t CommitBlock();

  // Encrypts the current data key and IV to the given |slot| using the given |key|.
  zx_status_t SealBlock(const crypto::Secret& key, key_slot_t slot);

  // Reads the block and parses and checks various fields before attempting to open it with the
  // given |key| corresponding to the given |slot|.
  zx_status_t UnsealBlock(const crypto::Secret& key, key_slot_t slot);

  ////////////////
  // Device methods

  virtual zx_status_t GetBlockInfo(BlockInfo* out) = 0;
  virtual zx_status_t GetFvmSliceSize(uint64_t* out) = 0;
  static const size_t MAX_SLICE_REGIONS = 16;
  virtual zx_status_t DoBlockFvmVsliceQuery(uint64_t vslice_start,
                                            SliceRegion ranges[MAX_SLICE_REGIONS],
                                            uint64_t* slice_count) = 0;
  virtual zx_status_t DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count) = 0;

  // Reads a block from the current offset on the underlying device.
  virtual zx_status_t Read() = 0;

  // Writes a block to the current offset on the underlying device.
  virtual zx_status_t Write() = 0;

  // Flushes all pending writes to the underlying device.
  virtual zx_status_t Flush() = 0;

  ////////////////
  // Fields

  // The underlying block device as accessed via DDK or FDIO.

  // The space reserved for metadata.
  uint64_t reserved_blocks_;
  uint64_t reserved_slices_;

  // Buffer holding the current block being examined, and its offset on the underlying device.
  crypto::Bytes block_;
  zx_off_t offset_;

  // The instance GUID for this device.
  crypto::Bytes guid_;
  // A copy of the entire header, used as AAD for the AEAD.
  crypto::Bytes header_;

  // The algorithm, lengths, and buffers for the key-wrapping AEAD.
  crypto::AEAD::Algorithm aead_;
  crypto::Secret wrap_key_;
  crypto::Bytes wrap_iv_;

  // The algorithm for data processing Cipher and length of wrapped key material.
  crypto::Cipher::Algorithm cipher_;
  crypto::Secret data_key_;
  crypto::Bytes data_iv_;
  size_t slot_len_;
  size_t num_key_slots_;

  // The digest used by the HKDF.
  crypto::digest::Algorithm digest_;
};

}  // namespace zxcrypt

#endif  // SRC_SECURITY_LIB_ZXCRYPT_VOLUME_H_
