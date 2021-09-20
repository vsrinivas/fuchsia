// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_ZXCRYPT_FDIO_VOLUME_H_
#define SRC_SECURITY_ZXCRYPT_FDIO_VOLUME_H_

#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

#include <memory>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

#include "src/security/fcrypto/secret.h"
#include "src/security/zxcrypt/volume.h"

namespace zxcrypt {

// Describes what activity we are performing: creating a new volume from
// scratch, or unsealing an existing volume.  Different activities may prefer
// different key sources for migration reasons.
enum Activity {
  Create,
  Unseal,
};

enum KeySourcePolicy {
  // Always uses a key of all zeroes.
  NullSource,

  // Always uses a key from the TEE; fail if not available
  TeeRequiredSource,

  // Always uses a key from the TEE for new volumes;
  // allows fallback to null key for unsealing volumes
  TeeTransitionalSource,

  // Attempts to use a key from the TEE for new volumes and unlocking, but
  // falls back to null key if TEE key fails
  TeeOpportunisticSource,

  // someday: TpmSource variants?
};

enum KeySource {
  kNullSource,
  kTeeSource,
};

fbl::Vector<KeySource> ComputeEffectiveCreatePolicy(KeySourcePolicy ksp);
fbl::Vector<KeySource> ComputeEffectiveUnsealPolicy(KeySourcePolicy ksp);
// Computes the ordered list of key sources that should be used in the context
// of |activity| under the key source policy |ksp|.
fbl::Vector<KeySource> ComputeEffectivePolicy(KeySourcePolicy ksp, Activity activity);

// Calls |callback| on a key provided by each key source appropriate for
// |activity| until either the callback returns ZX_OK or the callback has
// returned some error on all candidate key sources.  The caller must have
// access to /boot/config/zxcrypt in its namespace to use this function.
zx_status_t TryWithImplicitKeys(
    Activity activity, fbl::Function<zx_status_t(std::unique_ptr<uint8_t[]>, size_t)> callback);

// |zxcrypt::EncryptedVolumeClient| represents a channel to an instance of a bound
// zxcrypt device (named "zxcrypt" in the device tree).
class __EXPORT EncryptedVolumeClient {
 public:
  explicit EncryptedVolumeClient(zx::channel&& chan);

  // Request that the volume provided by the manager represented by |chan| be
  // formatted with the given key material/slot, destroying all previous data
  // and key slots.  This function will only succeed on a sealed volume.
  zx_status_t Format(const uint8_t* key, size_t key_len, uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // formatted with a product-defined implicit key associated with the specified
  // slot, destroying any previous superblock.  The caller must have access to
  // /boot/config/zxcrypt in its namespace to use this function.  This function
  // will only succeed on a sealed volume.
  zx_status_t FormatWithImplicitKey(uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // unsealed with the given key material/slot.  If successful, the driver
  // will create a child device named |unsealed| which exposes a block interface.
  zx_status_t Unseal(const uint8_t* key, size_t key_len, uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // unsealed with an product-defined implicit key associated with the specified
  // slot.  The caller must have access to /boot/config/zxcrypt in its
  // namespace to use this function.  If successful, the driver will create a
  // child device named |unsealed| which exposes a block interface.
  zx_status_t UnsealWithImplicitKey(uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // sealed.  After calling this method, it is an error to make any further
  // calls with this EncryptedVolumeClient.
  zx_status_t Seal();

  // Request that the volume provided by the manager represented by |chan| be
  // shredded, permanently rendering the device unable to be |Unseal|ed again in
  // the future.  This renders all data on the volume permanently inaccessible
  // once it is sealed.
  zx_status_t Shred();

 private:
  // The underlying zxcrypt device, accessed over FDIO
  zx::channel chan_;
};

// |zxcrypt::VolumeManager| manages access to a zxcrypt volume device.  In
// particular, it ensures that the driver is bound before returning a handle to
// the EncryptedVolumeClient.
//
// Due to the limitations of actions that involve multiple device drivers,
// VolumeManager requires access to both the block device we wish to run zxcrypt
// atop and the root of the device tree that contains said block device, so that
// we can discover child driver nodes in that tree via topological paths, which
// are currently the only way to obtain a handle to a newly-bound child.
class __EXPORT VolumeManager {
 public:
  explicit VolumeManager(fbl::unique_fd&& block_dev_fd, fbl::unique_fd&& devfs_root_fd);

  // Attempts to open the zxcrypt driver device associated with the underlying
  // block device described by |fd|, binding the driver if necessary,
  // and returning a channel to the zxcrypt device node.
  zx_status_t OpenClient(const zx::duration& timeout, zx::channel& out);

  // Attempts to open the block device representing the inner, unsealed block
  // device, at a device path of |/zxcrypt/unsealed/block| below the block device
  // represented by |block_dev_fd_|.  This will only work once you have called
  // |OpenClient| and used that handle to call |EncryptedVolumeClient::Unseal|
  // or |EncryptedVolumeClient::UnsealWithImplicitKey|.
  zx_status_t OpenInnerBlockDevice(const zx::duration& timeout, fbl::unique_fd* out);

 private:
  // OpenClient, but using a pre-created fdio_t.
  zx_status_t OpenClientWithCaller(fdio_cpp::UnownedFdioCaller& caller, const zx::duration& timeout,
                                   zx::channel& out);

  // Returns the topological path of the underlying block device, relative to
  // |devfs_root_fd|
  zx_status_t RelativeTopologicalPath(fdio_cpp::UnownedFdioCaller& caller, fbl::String* out);

  // The underlying block device, accessed over FDIO
  fbl::unique_fd block_dev_fd_;

  // The root of the device tree, needed to openat() related devices via
  // constructing relative topological paths.
  fbl::unique_fd devfs_root_fd_;
};

// |zxcrypt::FdioVolume| is a zxcrypt volume which does IO via a file descriptor
// to an underlying block device without any support from the zxcrypt driver
// implementation.  It can be used on the host to prepare zxcrypt images, and is
// often more convenient for testing.
class __EXPORT FdioVolume final : public Volume {
 public:
  explicit FdioVolume(fbl::unique_fd&& block_dev_fd);

  // Creates a new zxcrypt volume associated with the given file descriptor,
  // |block_dev_fd|, and returns it via |out|, if provided.  This will format
  // the block device as zxcrypt using the given |key|, which will be
  // associated with key slot 0.  This method takes ownership of
  // |block_dev_fd|.  Note that |key| is not strengthened
  // and MUST have cryptographic key length of at least 128 bits.
  static zx_status_t Create(fbl::unique_fd block_dev_fd, const crypto::Secret& key,
                            std::unique_ptr<FdioVolume>* out = nullptr);

  // Opens a zxcrypt volume on the block device described by |block_dev_fd|
  // using the |key| corresponding to given key |slot|.  This method takes
  // ownership of |block_dev_fd|.  Note that |key| is not strengthened and MUST
  // have cryptographic key length of at least 128 bits.  This is a convenience
  // method that calls |Init()| and then |FdioVolume::Unlock()|.
  static zx_status_t Unlock(fbl::unique_fd block_dev_fd, const crypto::Secret& key, key_slot_t slot,
                            std::unique_ptr<FdioVolume>* out);

  // Returns a new volume object corresponding to the block device given by
  // |block_dev_fd| and populated with the block and FVM information.
  static zx_status_t Init(fbl::unique_fd block_dev_fd, std::unique_ptr<FdioVolume>* out = nullptr);

  // Opens a zxcrypt volume on the block device described by |fd| using the |key| corresponding to
  // given key |slot|.
  zx_status_t Unlock(const crypto::Secret& key, key_slot_t slot);

  // Adds a given |key| to the given key |slot|.  This key can then be used to |Open| the
  // zxcrypt device.  This method can only be called if the volume belongs to libzxcrypt.
  zx_status_t Enroll(const crypto::Secret& key, key_slot_t slot);

  // Removes the root key in the given key |slot|.  This key can no longer be used to |Open| the
  // zxcrypt device.  This method can only be called if the volume belongs to libzxcrypt.
  zx_status_t Revoke(key_slot_t slot);

 private:
  friend class testing::TestDevice;

  // Retrieves the block and FVM information and adjusts it
  zx_status_t Init();

  zx_status_t GetBlockInfo(BlockInfo* out);
  zx_status_t GetFvmSliceSize(uint64_t* out);
  zx_status_t DoBlockFvmVsliceQuery(uint64_t vslice_start, SliceRegion ranges[MAX_SLICE_REGIONS],
                                    uint64_t* slice_count);
  zx_status_t DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count);

  // Reads a block from the current offset on the underlying device.
  zx_status_t Read();

  // Writes a block to the current offset on the underlying device.
  zx_status_t Write();

  // The underlying block device, accessed over FDIO
  fbl::unique_fd block_dev_fd_;
};

}  // namespace zxcrypt

#endif  // SRC_SECURITY_ZXCRYPT_FDIO_VOLUME_H_
