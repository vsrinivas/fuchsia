// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXCRYPT_FDIO_VOLUME_H_
#define ZXCRYPT_FDIO_VOLUME_H_

#include <lib/fdio/fdio.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>

#include <memory>

#include <crypto/secret.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zxcrypt/volume.h>

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

// Computes the ordered list of key sources that should be used in the context
// under the policy |ksp|.
fbl::Vector<KeySource> ComputeEffectiveCreatePolicy(KeySourcePolicy ksp);
fbl::Vector<KeySource> ComputeEffectiveUnsealPolicy(KeySourcePolicy ksp);

// Calls |callback| on a key provided by each key source in
// |ordered_key_sources| until either the callback returns ZX_OK or the callback
// has returned some error on all key sources in |ordered_key_sources|.
zx_status_t TryWithKeysFrom(
    const fbl::Vector<KeySource>& ordered_key_sources, Activity activity,
    fbl::Function<zx_status_t(std::unique_ptr<uint8_t[]>, size_t)> callback);

// |zxcrypt::FdioVolumeManager| represents a channel to an instance of a bound
// zxcrypt device (named "zxcrypt" in the device tree).
class __EXPORT FdioVolumeManager {
 public:
  explicit FdioVolumeManager(zx::channel&& chan);

  // Request that the volume provided by the manager represented by |chan| be
  // unsealed with the given key material/slot.  If successful, the driver
  // will create a child device named |unsealed| which exposes a block interface.
  zx_status_t Unseal(const uint8_t* key, size_t key_len, uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // unsealed with an product-defined device key associated with the specified
  // slot.  The caller must have access to /boot/config/zxcrypt in its
  // namespace to use this function.  If successful, the driver will create a
  // child device named |unsealed| which exposes a block interface.
  zx_status_t UnsealWithDeviceKey(uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // sealed.  After calling this method, it is an error to make any further
  // calls with this FdioVolumeManager.
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

// |zxcrypt::FdioVolume| is a zxcrypt volume which does IO via a file
// descriptor.  It can be used on the host to prepare zxcrypt images, and
// is often more convenient for testing.
class __EXPORT FdioVolume final : public Volume {
 public:
  explicit FdioVolume(fbl::unique_fd&& block_dev_fd, fbl::unique_fd&& devfs_root_fd);

  // Creates a new zxcrypt volume associated with the given file descriptor,
  // |block_dev_fd|, which must live in the device tree rooted at
  // |devfs_root_fd|, and returns it via |out|, if provided.  This will format
  // the block device as zxcrypt using the given |key|, which will be
  // associated with key slot 0.  This method takes ownership of
  // |block_dev_fd| and |devfs_root_fd|.  Note that |key| is not strengthened
  // and MUST have cryptographic key length of at least 128 bits.
  static zx_status_t Create(fbl::unique_fd block_dev_fd, fbl::unique_fd devfs_root_fd,
                            const crypto::Secret& key, std::unique_ptr<FdioVolume>* out = nullptr);

  // Does the same as |Create| but with the key provided by a product-defined
  // source.  The caller must have access to /boot/config/zxcrypt in its
  // namespace to use this function.
  static zx_status_t CreateWithDeviceKey(fbl::unique_fd&& block_dev_fd,
                                         fbl::unique_fd&& devfs_root_fd,
                                         std::unique_ptr<FdioVolume>* out);

  // Opens a zxcrypt volume on the block device described by |block_dev_fd|
  // (from the device tree rooted at |devfs_root_fd|) using the |key|
  // corresponding to given key |slot|.  The |block_dev_fd| parameter means
  // this method can be used from libzxcrypt. This method takes ownership of
  // |block_dev_fd| and |devfs_root_fd|.  Note that |key| is not strengthened
  // and MUST have cryptographic key length of at least 128 bits.
  static zx_status_t Unlock(fbl::unique_fd block_dev_fd, fbl::unique_fd devfs_root_fd,
                            const crypto::Secret& key, key_slot_t slot,
                            std::unique_ptr<FdioVolume>* out);

  // Opens a zxcrypt volume on the block device described by |block_dev_fd|
  // (from the device tree rooted at |devfs_root_fd|) using a key from a
  // product-defined source with the specified |slot|.  The caller must have
  // access to /boot/config/zxcrypt in their namespace to use this function.
  // This method takes ownership of |block_dev_fd| and |devfs_root_fd|.
  static zx_status_t UnlockWithDeviceKey(fbl::unique_fd block_dev_fd, fbl::unique_fd devfs_root_fd,
                                         key_slot_t slot, std::unique_ptr<FdioVolume>* out);

  // Returns a new volume object corresponding to the block device given by
  // |block_dev_fd| (which must live in the device tree hosted by
  // |devfs_root_fd|) and populated with the block and FVM information.
  static zx_status_t Init(fbl::unique_fd block_dev_fd, fbl::unique_fd devfs_root_fd,
                          std::unique_ptr<FdioVolume>* out = nullptr);

  // Opens a zxcrypt volume on the block device described by |fd| using the |key| corresponding to
  // given key |slot|.
  zx_status_t Unlock(const crypto::Secret& key, key_slot_t slot);

  // Attempts to open the zxcrypt driver device associated with the underlying
  // block device described by |fd|, binding the driver if necessary,
  // and returning a channel to the zxcrypt device node.
  zx_status_t OpenManager(const zx::duration& timeout, zx_handle_t* out);

  // Opens the block device exposed atop this volume and returns a file
  // descriptor to it via |out|, or fails if the volume isn't available within
  // |timeout|.
  zx_status_t Open(const zx::duration& timeout, fbl::unique_fd* out);

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

  // OpenManager, but using a pre-created fdio_t.
  zx_status_t OpenManagerWithCaller(fdio_cpp::UnownedFdioCaller& caller, const zx::duration& timeout,
                                    zx_handle_t* out);

  // Returns the topological path of the underlying block device, relative to
  // |devfs_root_fd|
  zx_status_t RelativeTopologicalPath(fdio_cpp::UnownedFdioCaller& caller, fbl::String* out);

  // The underlying block device, accessed over FDIO
  fbl::unique_fd block_dev_fd_;

  // The root of the device tree, needed to openat() related devices via
  // constructing relative topological paths.
  fbl::unique_fd devfs_root_fd_;
};

}  // namespace zxcrypt

#endif  // ZXCRYPT_FDIO_VOLUME_H_
