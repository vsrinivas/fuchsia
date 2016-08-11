// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GTT_H
#define GTT_H

#include "address_space.h"
#include "magma_util/address_space_allocator.h"
#include "magma_util/platform_buffer.h"
#include "magma_util/platform_device.h"
#include "register_io.h"
#include <memory>

class Gtt : public AddressSpace {
public:
    Gtt(std::shared_ptr<RegisterIo> reg_io);
    ~Gtt();

    uint64_t size() const { return size_; }

    bool Init(uint64_t gtt_size, std::shared_ptr<magma::PlatformDevice> platform_device);

    // AddressSpace overrides
    bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) override;
    bool Free(uint64_t addr) override;

    bool Clear(uint64_t addr) override;
    bool Insert(uint64_t addr, magma::PlatformBuffer* buffer, CachingType caching_type) override;

private:
    RegisterIo* reg_io() { return reg_io_.get(); }

    uint64_t pte_mmio_offset() { return mmio_->size() / 2; }

    magma::PlatformBuffer* scratch_buffer() { return scratch_.get(); }

    bool MapGttMmio(std::shared_ptr<magma::PlatformDevice> platform_device);
    void InitPrivatePat();
    bool InitScratch();
    bool InitPageTables(uint64_t start);
    bool Clear(uint64_t addr, uint64_t length);

private:
    std::shared_ptr<RegisterIo> reg_io_;
    std::unique_ptr<magma::PlatformMmio> mmio_;
    std::unique_ptr<magma::PlatformBuffer> scratch_;
    std::unique_ptr<AddressSpaceAllocator> allocator_;
    uint64_t scratch_gpu_addr_;
    uint64_t size_;

    friend class TestGtt;
};

#endif // GTT_H
