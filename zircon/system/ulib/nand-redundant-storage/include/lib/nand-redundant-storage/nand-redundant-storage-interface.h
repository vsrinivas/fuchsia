// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <vector>

#include <lib/mtd/nand-interface.h>
#include <zircon/types.h>

namespace nand_rs {

// Base interface for a NAND-based redundant storage writer/reader.
class NandRedundantStorageInterface {
public:
    virtual ~NandRedundantStorageInterface() {}

    // Writes a buffer to the NAND storage device.
    //
    // This overwrites anything stored on the device on the block level, and can
    // potentially erase the entire device's storage even when requesting to
    // store a single copy of a small file.
    //
    // Requires a non-empty buffer that is at least 12-bytes smaller than the
    // erase block size (this leaves room for a recovery header).
    //
    // |num_copies| must be no larger than the total NAND interface's storage
    // capacity divided by the erase block size.
    //
    // |num_copies_written| returns the number of copies successfully written.
    //
    // Each copy of the buffer will be stored on one erase block of the NAND
    // device with an included 12 byte recovery header.
    //
    // Return Values:
    // --------------
    //
    // ZX_OK -- in the event that at least one copy of |buffer| is written to
    //          the NAND interface successfully. The total number of copies
    //          will be written to the out variable |num_copies_written|.
    //
    // ZX_ERR_NO_SPACE -- in the event that it is not possible to write any
    //                    copies. This would be if there are absolutely no good
    //                    blocks on the NAND interface.
    virtual zx_status_t WriteBuffer(const std::vector<uint8_t>& buffer,
                                    uint32_t num_copies,
                                    uint32_t* num_copies_written) = 0;

    // Attempts to read from a NAND interface previously written to with
    // WriteBuffer.
    //
    // If this is not possible ZX_ERR_IO will be returned.
    //
    // On success, out_buffer will be written to with the contents stored in the
    // NAND interface sans 12-byte header.
    virtual zx_status_t ReadToBuffer(std::vector<uint8_t>* out_buffer) = 0;
};

} // namespace nand_rs
