// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <crypto/secret.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <zxcrypt/volume.h>

namespace zxcrypt {

// |zxcrypt::FdioVolumeManager| represents a channel to an instance of a bound
// zxcrypt device (named "zxcrypt" in the device tree).
class FdioVolumeManager {
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

private:
    // The underlying zxcrypt device, accessed over FDIO
    zx::channel chan_;
};

// |zxcrypt::FdioVolume| is a zxcrypt volume which does IO via a file
// descriptor.  It can be used on the host to prepare zxcrypt images, and
// is often more convenient for testing.
class FdioVolume final : public Volume {
public:
    explicit FdioVolume(fbl::unique_fd&& fd);


    // Creates a new zxcrypt volume associated with the given file descriptor, |fd| and returns it
    // via |out|, if provided.  This will format the block device as zxcrypt using the given |key|,
    // which will be associated with key slot 0. This method takes ownership of |fd|.  Note that
    // |key| is not strengthened and MUST have cryptographic key length of at least 128 bits.
    static zx_status_t Create(fbl::unique_fd fd, const crypto::Secret& key,
                              fbl::unique_ptr<FdioVolume>* out = nullptr);

    // Does the same as |Create| but with the key provided by a product-defined
    // source.  The caller must have access to /boot/config/zxcrypt in its
    // namespace to use this function.
    static zx_status_t CreateWithDeviceKey(fbl::unique_fd&& fd,
                                           fbl::unique_ptr<FdioVolume>* out);

    // Opens a zxcrypt volume on the block device described by |fd| using the |key| corresponding to
    // given key |slot|.  The |fd| parameter means this method can be used from libzxcrypt. This
    // method takes ownership of |fd|.  Note that |key| is not strengthened and MUST have
    // cryptographic key length of at least 128 bits.
    static zx_status_t Unlock(fbl::unique_fd fd, const crypto::Secret& key, key_slot_t slot,
                              fbl::unique_ptr<FdioVolume>* out);

    // Opens a zxcrypt volume on the block device described by |fd| using a key
    // from a product-defined source with the specified |slot|.  The caller must
    // have access to /boot/config/zxcrypt in their namespace to use this
    // function.  This method takes ownership of |fd|.
    static zx_status_t UnlockWithDeviceKey(fbl::unique_fd fd, key_slot_t slot,
                                           fbl::unique_ptr<FdioVolume>* out);

    // Returns a new volume object corresponding to the block device given by |fd| and populated
    // with the block and FVM information.
    static zx_status_t Init(fbl::unique_fd fd, fbl::unique_ptr<FdioVolume>* out = nullptr);

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
    zx_status_t DoBlockFvmVsliceQuery(uint64_t vslice_start,
                                      SliceRegion ranges[MAX_SLICE_REGIONS],
                                      uint64_t* slice_count);
    zx_status_t DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count);

    // Reads a block from the current offset on the underlying device.
    zx_status_t Read();

    // Writes a block to the current offset on the underlying device.
    zx_status_t Write();

    // OpenManager, but using a pre-created fdio_t.
    zx_status_t OpenManagerWithCaller(fzl::UnownedFdioCaller& caller,
                                      const zx::duration& timeout,
                                      zx_handle_t* out);

    // Returns the topological path of the underlying block device
    zx_status_t TopologicalPath(fzl::UnownedFdioCaller& caller, fbl::String* out);

    // The underlying block device, accessed over FDIO
    fbl::unique_fd fd_;
};

} // namespace zxcrypt
