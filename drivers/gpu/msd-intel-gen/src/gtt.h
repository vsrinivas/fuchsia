// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTT_H
#define GTT_H

#include <memory>
#include "address_space.h"
#include "platform_pci_device.h"

class Gtt : public AddressSpace {
public:
    class Owner {
    public:
        virtual magma::PlatformPciDevice* platform_device() = 0;
    };

    Gtt() : AddressSpace(ADDRESS_SPACE_GGTT, nullptr) {}

    virtual uint64_t Size() const = 0;
    virtual bool Init(uint64_t gtt_size) = 0;

    static std::unique_ptr<Gtt> CreateShim(Owner* owner);
    static std::unique_ptr<Gtt> CreateCore(Owner* owner);

    friend class TestGtt;
};

#endif // GTT_H
