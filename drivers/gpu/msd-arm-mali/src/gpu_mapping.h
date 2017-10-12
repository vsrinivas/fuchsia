// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_H_
#define GPU_MAPPING_H_

#include <cstdint>
#include <memory>

class MsdArmBuffer;
class MsdArmConnection;

// A buffer may be mapped into a connection at multiple virtual addresses. The
// connection owns the GpuMapping, so |owner_| is always valid. The buffer
// deletes all the mappings it owns before it's destroyed, so that's why
// |buffer_| is always valid.
class GpuMapping {
public:
    class Owner {
    public:
        virtual bool RemoveMapping(uint64_t address) = 0;
    };

    GpuMapping(uint64_t addr, uint64_t size, uint64_t flags, Owner* owner,
               std::shared_ptr<MsdArmBuffer> buffer);

    ~GpuMapping();

    uint64_t gpu_va() const { return addr_; }
    uint64_t size() const { return size_; }
    uint64_t flags() const { return flags_; }

    MsdArmBuffer* buffer() const;
    void Remove() { owner_->RemoveMapping(addr_); }

private:
    uint64_t addr_;
    uint64_t size_;
    uint64_t flags_;
    Owner* owner_;
    std::weak_ptr<MsdArmBuffer> buffer_;
};

#endif // GPU_MAPPING_H_
