// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <climits>
#include <lib/hermetic-compute/hermetic-compute.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

// VmoSpan represents a region (offset and size) within a VMO.  It holds
// the VMO handle but does not own it.  VmoSpan is usually an ephemeral
// object created in a hermetic argument list.  Its main purpose is to be
// matched by the HermeticExportAgent specialization below.
//
// A VmoSpan is exported as a buffer pointer and byte size.  The
// corresponding import type can be std::pair<void*, size_t> or
// std::string_view, std::basic_string_view<SomeType>, etc.
//
// VmoSpan provides read-only access to the hermetic engine.
// WritableVmoSpan provides writable access to the hermetic engine.
//
// **NOTE!** If the offset and size are not page-aligned, then the partial
// pages around the span will also be accessible to the hermetic engine!
// This is disallowed by assertion in VmoSpan and WritableVmoSpan, and only
// permitted in LeakyVmoSpan.

template <bool Leaky = false, bool Writable = false>
class VmoSpan {
public:
    VmoSpan(const zx::vmo& vmo,  uint64_t offset, size_t size) :
        vmo_(zx::unowned_vmo{vmo}), offset_(offset), size_(size) {
        if constexpr (!Leaky) {
            ZX_DEBUG_ASSERT(offset % PAGE_SIZE == 0);
            ZX_DEBUG_ASSERT(size % PAGE_SIZE == 0);
        }
    }

    zx::unowned_vmo vmo() const { return zx::unowned_vmo{vmo_->get()}; }
    uint64_t offset() const { return offset_; }
    size_t size() const { return size_; }

    uint64_t map_offset() const {
        return offset() & -uint64_t{PAGE_SIZE};
    }

    size_t map_size() const {
        size_t result = offset() + size() - map_offset();
        return (result + PAGE_SIZE - 1) & -size_t{PAGE_SIZE};
    }

private:
    zx::unowned_vmo vmo_;
    uint64_t offset_ = 0;
    size_t size_ = 0;
};

using LeakyVmoSpan = VmoSpan<true, false>;
using WritableVmoSpan = VmoSpan<false, true>;

template <bool Leaky, bool Writable>
struct HermeticExportAgent<VmoSpan<Leaky, Writable>> :
    public HermeticExportAgentBase<VmoSpan<Leaky, Writable>> {
    using type = VmoSpan<Leaky, Writable>;
    using Base = HermeticExportAgentBase<type>;
    explicit HermeticExportAgent(HermeticComputeProcess::Launcher& launcher) :
        Base(launcher) {}

    std::tuple<uintptr_t, size_t>  operator()(const type& x) {
        uintptr_t ptr = this->launcher().Map(
            *x.vmo(), x.map_offset(), x.map_size(), Writable);
        return {x.offset() - x.map_offset() + ptr, x.size()};
    }
};
