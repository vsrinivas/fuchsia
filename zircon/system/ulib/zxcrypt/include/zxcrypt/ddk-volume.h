// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/unique_ptr.h>
#include <zxcrypt/volume.h>

namespace zxcrypt {

class DdkVolume final : public Volume {

public:
    explicit DdkVolume(zx_device_t* dev);

    // Unlocks a zxcrypt volume on the block device described by |dev| using the |key| corresponding
    // to given key |slot|.
    static zx_status_t Unlock(zx_device_t* dev, const crypto::Secret& key, key_slot_t slot,
                              fbl::unique_ptr<DdkVolume>* out);

    // Uses the data key material to initialize |cipher| for the given |direction|.
    zx_status_t Bind(crypto::Cipher::Direction direction, crypto::Cipher* cipher) const;

private:
    // Retrieve the block/FVM information and adjust it
    zx_status_t Init();

    // Opens a zxcrypt volume on the underlying block device using the |key|
    // corresponding to given key |slot|.
    zx_status_t Unlock(const crypto::Secret& key, key_slot_t slot);

    // Sends an I/O control message to the underlying device and reads the
    // response.
    zx_status_t Ioctl(int op, const void* in, size_t in_len, void* out, size_t out_len);

    // Reads a block from the current offset on the underlying device.
    zx_status_t Read();

    // Writes a block to the current offset on the underlying device.
    zx_status_t Write();

    // The underlying block device, accessed via DDK
    zx_device_t* dev_;
};

} // namespace zxcrypt
