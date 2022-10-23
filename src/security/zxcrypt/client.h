// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_ZXCRYPT_CLIENT_H_
#define SRC_SECURITY_ZXCRYPT_CLIENT_H_

#include <fidl/fuchsia.hardware.block.encrypted/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>

#include <memory>

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

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

// Reads /pkg/config/zxcrypt to determine what key source policy was selected for this product at
// build time.
//
// Returns the appropriate KeySourcePolicy value if the file contents exactly match a known
// configuration value.
// Returns ZX_ERR_NOT_FOUND if the config file was not present
// Returns ZX_ERR_IO if the config file could not be read
// Returns ZX_ERR_BAD_STATE if the config value was not recognized.
zx::result<KeySourcePolicy> SelectKeySourcePolicy();

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
    Activity activity, fit::function<zx_status_t(std::unique_ptr<uint8_t[]>, size_t)> callback);

// |zxcrypt::EncryptedVolumeClient| represents a channel to an instance of a bound
// zxcrypt device (named "zxcrypt" in the device tree).
class __EXPORT EncryptedVolumeClient {
 public:
  explicit EncryptedVolumeClient(zx::channel&& channel);

  // Request that the volume provided by the manager represented by |chan| be
  // formatted with the given key material/slot, destroying all previous data
  // and key slots.  This function will only succeed on a sealed volume.
  zx_status_t Format(const uint8_t* key, size_t key_len, uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // formatted with a product-defined device key associated with the specified
  // slot, destroying any previous superblock.  The caller must have access to
  // /boot/config/zxcrypt in its namespace to use this function.  This function
  // will only succeed on a sealed volume.
  zx_status_t FormatWithImplicitKey(uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // unsealed with the given key material/slot.  If successful, the driver
  // will create a child device named |unsealed| which exposes a block interface.
  zx_status_t Unseal(const uint8_t* key, size_t key_len, uint8_t slot);

  // Request that the volume provided by the manager represented by |chan| be
  // unsealed with an product-defined device key associated with the specified
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
  fidl::ClientEnd<fuchsia_hardware_block_encrypted::DeviceManager> client_end_;
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
  // OpenManager, but using a pre-created fdio_t.
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

}  // namespace zxcrypt

#endif  // SRC_SECURITY_ZXCRYPT_CLIENT_H_
