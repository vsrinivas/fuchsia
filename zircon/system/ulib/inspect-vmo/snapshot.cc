// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect-vmo/block.h>
#include <lib/inspect-vmo/snapshot.h>

namespace inspect {
namespace vmo {

using internal::Block;

Snapshot::Snapshot(fbl::Array<uint8_t> buffer)
    : buffer_(std::move(buffer)) {}

zx_status_t Snapshot::Create(fbl::Array<uint8_t> buffer, Snapshot* out_snapshot) {
    ZX_ASSERT(out_snapshot);
    // A buffer does not have concurrent writers or observers, so we don't use
    // this.
    uint64_t unused;
    // Verify that the buffer can, in fact, be parsed as a snapshot.
    zx_status_t status = Snapshot::ParseHeader(buffer.begin(), &unused);
    if (status != ZX_OK) {
        return status;
    }
    *out_snapshot = Snapshot(std::move(buffer));
    if (!*out_snapshot) {
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

zx_status_t Snapshot::Create(const zx::vmo& vmo, Snapshot* out_snapshot) {
    return Snapshot::Create(std::move(vmo), kDefaultOptions, out_snapshot);
}

zx_status_t Snapshot::Create(const zx::vmo& vmo, Options options, Snapshot* out_snapshot) {
    return Snapshot::Create(std::move(vmo), std::move(options), nullptr, out_snapshot);
}

zx_status_t Snapshot::Create(const zx::vmo& vmo, Options options, ReadObserver read_observer,
                             Snapshot* out_snapshot) {
    size_t tries_left = options.read_attempts;

    zx_status_t status;
    fbl::Array<uint8_t> buffer;

    while (tries_left-- > 0) {
        size_t size;
        status = vmo.get_size(&size);
        if (status != ZX_OK) {
            return status;
        }
        if (size < sizeof(internal::Block)) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        if (buffer.size() != size) {
            buffer.reset(new uint8_t[size], size);
        }

        status = Snapshot::Read(vmo, sizeof(internal::Block), buffer.begin());
        if (status != ZX_OK) {
            return status;
        }
        if (read_observer) {
            read_observer(buffer.begin(), sizeof(internal::Block));
        }

        uint64_t generation;
        status = Snapshot::ParseHeader(buffer.begin(), &generation);
        if (status != ZX_OK) {
            return status;
        }

        if (!options.skip_consistency_check && generation % 2 != 0) {
            continue;
        }

        status = Snapshot::Read(vmo, size, buffer.begin());
        if (status != ZX_OK) {
            return status;
        }
        if (read_observer) {
            read_observer(buffer.begin(), sizeof(size));
        }

        uint64_t new_generation;
        status = Snapshot::ParseHeader(buffer.begin(), &new_generation);
        if (status != ZX_OK) {
            return status;
        }
        if (!options.skip_consistency_check && generation != new_generation) {
            continue;
        }

        size_t new_size;
        if (vmo.get_size(&new_size) != ZX_OK) {
            return ZX_ERR_INTERNAL;
        }
        if (new_size != size) {
            continue;
        }

        *out_snapshot = Snapshot(std::move(buffer));

        return ZX_OK;
    }

    return ZX_ERR_INTERNAL;
}

zx_status_t Snapshot::Read(const zx::vmo& vmo, size_t size, uint8_t* buffer) {
    memset(buffer, 0, size);
    return vmo.read(buffer, 0, size);
}

zx_status_t Snapshot::ParseHeader(uint8_t* buffer, uint64_t* out_generation_count) {
    Block* block = reinterpret_cast<Block*>(buffer);
    if (memcmp(&block->header_data[4], kMagicNumber, 4) != 0) {
        return ZX_ERR_INTERNAL;
    }
    *out_generation_count = block->payload.u64;
    return ZX_OK;
}

internal::Block* Snapshot::GetBlock(internal::BlockIndex index) const {
    if (index >= IndexForOffset(buffer_.size())) {
        return nullptr;
    }
    return reinterpret_cast<internal::Block*>(buffer_.begin() + index * kMinOrderSize);
}

} // namespace vmo
} // namespace inspect
