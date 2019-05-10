// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/internal.h>

#include <stdint.h>

namespace hwreg {

// Wrap MMIO for easier testing of device drivers

class RegisterIo {
public:
    RegisterIo(volatile void* mmio) : mmio_(reinterpret_cast<uintptr_t>(mmio)) {
    }

    // Write |val| to the |sizeof(IntType)| byte field located |offset| bytes from
    // |base()|.
    template <class IntType> void Write(uint32_t offset, IntType val) {
        static_assert(internal::IsSupportedInt<IntType>::value,
                      "unsupported register access width");
        auto ptr = reinterpret_cast<volatile IntType*>(mmio_ + offset);
        *ptr = val;
    }

    // Read the value of the |sizeof(IntType)| byte field located |offset| bytes from
    // |base()|.
    template <class IntType> IntType Read(uint32_t offset) {
        static_assert(internal::IsSupportedInt<IntType>::value,
                      "unsupported register access width");
        auto ptr = reinterpret_cast<volatile IntType*>(mmio_ + offset);
        return *ptr;
    }

    uintptr_t base() const { return mmio_; }

private:
    const uintptr_t mmio_;
};

} // namespace hwreg
