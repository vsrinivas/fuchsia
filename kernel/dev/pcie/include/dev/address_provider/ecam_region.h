// Copyright 2018 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/intrusive_wavl_tree.h>
#include <sys/types.h>
#include <zircon/types.h>

struct PciEcamRegion {
    paddr_t phys_base; // Physical address of the memory mapped config region.
    size_t size;       // Size (in bytes) of the memory mapped config region.
    uint8_t bus_start; // Inclusive ID of the first bus controlled by this region.
    uint8_t bus_end;   // Inclusive ID of the last bus controlled by this region.
};

class MappedEcamRegion : public fbl::WAVLTreeContainable<fbl::unique_ptr<MappedEcamRegion>> {
public:
    explicit MappedEcamRegion(const PciEcamRegion& ecam)
        : ecam_(ecam) {}
    ~MappedEcamRegion();

    const PciEcamRegion& ecam() const { return ecam_; }
    void* vaddr() const { return vaddr_; }
    zx_status_t MapEcam();

    // WAVLTree properties
    uint8_t GetKey() const { return ecam_.bus_start; }

private:
    PciEcamRegion ecam_;
    void* vaddr_ = nullptr;
};