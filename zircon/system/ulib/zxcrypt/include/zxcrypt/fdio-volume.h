// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <crypto/secret.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <zxcrypt/volume.h>

// |zxcrypt::FdioVolume| is a zxcrypt volume which does IO via a file
// descriptor.  It can be used on the host to prepare zxcrypt images, and
// is often more convenient for testing.
namespace zxcrypt {

class FdioVolume final : public Volume {

public:
    explicit FdioVolume(fbl::unique_fd&& fd);


    // Creates a new zxcrypt volume associated with the given file descriptor, |fd| and returns it
    // via |out|, if provided.  This will format the block device as zxcrypt using the given |key|,
    // which will be associated with key slot 0. This method takes ownership of |fd|.
    static zx_status_t Create(fbl::unique_fd fd, const crypto::Secret& key,
                              fbl::unique_ptr<FdioVolume>* out = nullptr);

    // Opens a zxcrypt volume on the block device described by |fd| using the |key| corresponding to
    // given key |slot|.  The |fd| parameter means this method can be used from libzxcrypt. This
    // method takes ownership of |fd|.
    static zx_status_t Unlock(fbl::unique_fd fd, const crypto::Secret& key, key_slot_t slot,
                              fbl::unique_ptr<FdioVolume>* out);

    // Returns a new volume object corresponding to the block device given by |fd| and populated
    // with the block and FVM information.
    static zx_status_t Init(fbl::unique_fd fd, fbl::unique_ptr<FdioVolume>* out = nullptr);

    // Opens a zxcrypt volume on the block device described by |fd| using the |key| corresponding to
    // given key |slot|.
    zx_status_t Unlock(const crypto::Secret& key, key_slot_t slot);

    // Opens the zxcrypt volume and returns a file descriptor to it via |out|, or fails if the
    // volume isn't available within |timeout|.
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
    zx_status_t DoBlockFvmVsliceQuery(uint64_t vslice_start, SliceRegion ranges[MAX_SLICE_REGIONS], uint64_t* slice_count);
    zx_status_t DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count);

    // Reads a block from the current offset on the underlying device.
    zx_status_t Read();

    // Writes a block to the current offset on the underlying device.
    zx_status_t Write();

    // The underlying block device, accessed over FDIO
    fbl::unique_fd fd_;
};

} // namespace zxcrypt
