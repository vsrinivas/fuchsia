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

#include "gtt.h"
#include "magma_util/platform_mmio.h"
#include "mock/mock_mmio.h"
#include "register_defs.h"
#include "gtest/gtest.h"

class TestGtt {
public:
    static PlatformBuffer* scratch_buffer(Gtt* gtt) { return gtt->scratch_buffer(); }
};

namespace {

class MockPlatformDevice : public magma::PlatformDevice {
public:
    MockPlatformDevice(uint64_t bar0_size) : bar0_size_(bar0_size) {}

    std::unique_ptr<magma::PlatformMmio>
    CpuMapPciMmio(unsigned int pci_bar, magma::PlatformMmio::CachePolicy cache_policy) override
    {
        DASSERT(!mmio_);

        if (pci_bar != 0)
            return DRETP(nullptr, "");

        std::unique_ptr<MockMmio> mmio = MockMmio::Create(bar0_size_);
        mmio_ = mmio.get();

        return mmio;
    }

    MockMmio* mmio() { return mmio_; }

private:
    uint64_t bar0_size_;
    MockMmio* mmio_{};
};

} // namespace

static uint32_t cache_bits(CachingType caching_type)
{
    switch (caching_type) {
    case CACHING_NONE:
        return (1 << 3) | (1 << 4); // 3
    case CACHING_WRITE_THROUGH:
        return (1 << 4); // 3
    case CACHING_LLC:
        return 1 << 7; // 4
    }
}

static void check_pte_entries_clear(magma::PlatformMmio* mmio, uint64_t gpu_addr, uint64_t size,
                                    uint64_t bus_addr)
{
    ASSERT_NE(mmio, nullptr);

    uint64_t* pte_array =
        reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(mmio->addr()) + mmio->size() / 2);

    uint32_t page_count = size >> PAGE_SHIFT;

    for (unsigned int i = 0; i < page_count; i++) {
        uint64_t pte = pte_array[(gpu_addr >> PAGE_SHIFT) + i];
        EXPECT_EQ(pte & ~(PAGE_SIZE - 1), bus_addr);
        EXPECT_TRUE(pte & 0x1); // page present
        EXPECT_TRUE(pte & 0x3); // rw
        EXPECT_EQ(pte & cache_bits(CACHING_LLC), cache_bits(CACHING_LLC));
    }
}

// size_bits: 1 (2MB), 2 (4MB), 3 (8MB)
static void test_gtt_init(unsigned int size_bits)
{
    ASSERT_EQ(true, size_bits == 1 || size_bits == 2 || size_bits == 3);
    uint64_t gtt_size = (1 << size_bits) * 1024 * 1024;
    uint64_t reg_size = gtt_size;

    std::shared_ptr<MockPlatformDevice> platform_device(
        new MockPlatformDevice(reg_size + gtt_size));

    // For registers provide only half the bar
    std::shared_ptr<RegisterIo> reg_io(new RegisterIo(MockMmio::Create(reg_size)));

    std::unique_ptr<Gtt> gtt(new Gtt(reg_io));

    bool ret = gtt->Init(gtt_size, platform_device);
    EXPECT_TRUE(ret);

    uint64_t scratch_bus_addr;
    ret = TestGtt::scratch_buffer(gtt.get())->MapPageBus(0, &scratch_bus_addr);
    EXPECT_TRUE(ret);

    auto mmio = platform_device->mmio();
    ASSERT_NE(mmio, nullptr);

    check_pte_entries_clear(mmio, 0, mmio->size(), scratch_bus_addr);

    ret = TestGtt::scratch_buffer(gtt.get())->UnmapPageBus(0);
    EXPECT_TRUE(ret);
}

TEST(Gtt, Init)
{
    test_gtt_init(3);
    test_gtt_init(2);
    test_gtt_init(1);
}

static void check_pte_entries(magma::PlatformMmio* mmio, PlatformBuffer* buffer, uint64_t gpu_addr,
                              CachingType caching_type)
{
    ASSERT_NE(mmio, nullptr);

    uint64_t* pte_array =
        reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(mmio->addr()) + mmio->size() / 2);

    uint32_t page_count;
    bool ret = buffer->PinnedPageCount(&page_count);
    EXPECT_TRUE(true);

    for (unsigned int i = 0; i < page_count; i++) {
        uint64_t bus_addr;
        ret = buffer->MapPageBus(i, &bus_addr);
        EXPECT_TRUE(ret);

        uint64_t pte = pte_array[(gpu_addr >> PAGE_SHIFT) + i];
        EXPECT_EQ(pte & ~(PAGE_SIZE - 1), bus_addr);
        EXPECT_TRUE(pte & 0x1); // page present
        EXPECT_TRUE(pte & 0x3); // rw
        EXPECT_EQ(pte & cache_bits(caching_type), cache_bits(caching_type));

        ret = buffer->UnmapPageBus(i);
        EXPECT_TRUE(ret);
    }
}

TEST(Gtt, Insert)
{
    uint64_t gtt_size = 8ULL * 1024 * 1024;
    uint64_t bar0_size = gtt_size * 2;

    std::shared_ptr<MockPlatformDevice> platform_device(new MockPlatformDevice(bar0_size));

    std::shared_ptr<RegisterIo> reg_io(new RegisterIo(MockMmio::Create(bar0_size)));

    std::unique_ptr<Gtt> gtt(new Gtt(reg_io));

    bool ret = gtt->Init(gtt_size, platform_device);
    EXPECT_EQ(ret, true);

    uint32_t pat_index_low = reg_io->Read32(BDW_PAT_INDEX_LOW);
    EXPECT_EQ(pat_index_low, 0xA0907u);

    uint32_t pat_index_high = reg_io->Read32(BDW_PAT_INDEX_HIGH);
    EXPECT_EQ(pat_index_high, 0x3B2B1B0Bu);

    uint64_t scratch_bus_addr;
    ret = TestGtt::scratch_buffer(gtt.get())->MapPageBus(0, &scratch_bus_addr);
    EXPECT_TRUE(ret);

    // create some buffers
    std::vector<uint64_t> addr(2);
    std::vector<std::unique_ptr<PlatformBuffer>> buffer(2);

    buffer[0] = PlatformBuffer::Create(1000);
    ret = gtt->Alloc(buffer[0]->size(), 0, &addr[0]);
    EXPECT_EQ(ret, true);

    buffer[1] = PlatformBuffer::Create(10000);
    ret = gtt->Alloc(buffer[1]->size(), 0, &addr[1]);
    EXPECT_EQ(ret, true);

    // Try to insert without pinning
    ret = gtt->Insert(addr[0], buffer[0].get(), CACHING_NONE);
    EXPECT_EQ(ret, false);

    ret = buffer[0]->PinPages();
    EXPECT_EQ(ret, true);
    ret = buffer[1]->PinPages();
    EXPECT_EQ(ret, true);

    // Mismatch addr and buffer
    ret = gtt->Insert(addr[1], buffer[0].get(), CACHING_NONE);
    EXPECT_EQ(ret, false);

    // Totally bogus addr
    ret = gtt->Insert(0xdead1000, buffer[0].get(), CACHING_NONE);
    EXPECT_EQ(ret, false);

    // Correct
    ret = gtt->Insert(addr[0], buffer[0].get(), CACHING_NONE);
    EXPECT_EQ(ret, true);

    check_pte_entries(platform_device->mmio(), buffer[0].get(), addr[0], CACHING_NONE);

    // Also correct
    ret = gtt->Insert(addr[1], buffer[1].get(), CACHING_NONE);
    EXPECT_EQ(ret, true);

    check_pte_entries(platform_device->mmio(), buffer[1].get(), addr[1], CACHING_NONE);

    // Bogus addr
    ret = gtt->Clear(0xdead1000);
    EXPECT_EQ(ret, false);

    // Cool
    ret = gtt->Clear(addr[1]);
    EXPECT_EQ(ret, true);

    check_pte_entries_clear(platform_device->mmio(), addr[1], buffer[1]->size(), scratch_bus_addr);

    ret = gtt->Clear(addr[0]);
    EXPECT_EQ(ret, true);

    check_pte_entries_clear(platform_device->mmio(), addr[0], buffer[0]->size(), scratch_bus_addr);

    // Bogus addr
    ret = gtt->Free(0xdead1000);
    EXPECT_EQ(ret, false);

    // Cool
    ret = gtt->Free(addr[0]);
    EXPECT_EQ(ret, true);
    ret = gtt->Free(addr[1]);
    EXPECT_EQ(ret, true);

    ret = TestGtt::scratch_buffer(gtt.get())->UnmapPageBus(0);
    EXPECT_TRUE(ret);
}
