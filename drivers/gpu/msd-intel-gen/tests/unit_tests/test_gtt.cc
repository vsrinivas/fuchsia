// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtt.h"
#include "mock/mock_mmio.h"
#include "platform_mmio.h"
#include "registers.h"
#include "gtest/gtest.h"

class TestGtt {
public:
    static magma::PlatformBuffer* scratch_buffer(Gtt* gtt) { return gtt->scratch_buffer(); }
};

namespace {

class MockPlatformDevice : public magma::PlatformDevice {
public:
    MockPlatformDevice(uint64_t bar0_size) : bar0_size_(bar0_size) {}

    void* GetDeviceHandle() override { return nullptr; }

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

uint32_t cache_bits(CachingType caching_type)
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

void check_pte_entries_clear(magma::PlatformMmio* mmio, uint64_t gpu_addr, uint64_t size,
                             uint64_t bus_addr)
{
    ASSERT_NE(mmio, nullptr);

    uint64_t* pte_array =
        reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(mmio->addr()) + mmio->size() / 2);

    uint32_t page_count = size >> PAGE_SHIFT;

    // Note: <= is intentional here to accout for ofer-fetch protection page
    for (unsigned int i = 0; i <= page_count; i++) {
        uint64_t pte = pte_array[(gpu_addr >> PAGE_SHIFT) + i];
        EXPECT_EQ(pte & ~(PAGE_SIZE - 1), bus_addr);
        EXPECT_FALSE(pte & 0x1); // page should not be present
        EXPECT_TRUE(pte & 0x3); // rw
        EXPECT_EQ(pte & cache_bits(CACHING_LLC), cache_bits(CACHING_LLC));
    }
}

void check_pte_entries(magma::PlatformMmio* mmio, magma::PlatformBuffer* buffer, uint64_t gpu_addr,
                       uint64_t scratch_bus_addr, CachingType caching_type)
{
    ASSERT_NE(mmio, nullptr);

    uint64_t* pte_array =
        reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(mmio->addr()) + mmio->size() / 2);

    ASSERT_TRUE(magma::is_page_aligned(buffer->size()));
    uint32_t page_count = buffer->size() / PAGE_SIZE;

    for (unsigned int i = 0; i < page_count; i++) {
        uint64_t bus_addr;
        EXPECT_TRUE(buffer->MapPageBus(i, &bus_addr));

        uint64_t pte = pte_array[(gpu_addr >> PAGE_SHIFT) + i];
        EXPECT_EQ(pte & ~(PAGE_SIZE - 1), bus_addr);
        EXPECT_TRUE(pte & 0x1); // page present
        EXPECT_TRUE(pte & 0x3); // rw
        EXPECT_EQ(pte & cache_bits(caching_type), cache_bits(caching_type));

        EXPECT_TRUE(buffer->UnmapPageBus(i));
    }

    uint64_t pte = pte_array[(gpu_addr >> PAGE_SHIFT) + page_count];
    EXPECT_EQ(pte & ~(PAGE_SIZE - 1), scratch_bus_addr);
    EXPECT_FALSE(pte & 0x1); // page present
    EXPECT_TRUE(pte & 0x3);  // rw
    EXPECT_EQ(pte & cache_bits(caching_type), cache_bits(caching_type));
}

class TestDevice : public Gtt::Owner {
public:
    // size_bits: 1 (2MB), 2 (4MB), 3 (8MB)
    void Init(unsigned int size_bits)
    {
        ASSERT_EQ(true, size_bits == 1 || size_bits == 2 || size_bits == 3);
        uint64_t gtt_size = (1 << size_bits) * 1024 * 1024;
        uint64_t reg_size = gtt_size;

        platform_device =
            std::unique_ptr<MockPlatformDevice>(new MockPlatformDevice(reg_size + gtt_size));
        reg_io = std::unique_ptr<RegisterIo>(new RegisterIo(MockMmio::Create(reg_size)));
        gtt = std::unique_ptr<Gtt>(new Gtt(this));

        bool ret = gtt->Init(gtt_size, platform_device.get());
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

    void Insert()
    {
        uint64_t gtt_size = 8ULL * 1024 * 1024;
        uint64_t bar0_size = gtt_size * 2;

        platform_device = std::shared_ptr<MockPlatformDevice>(new MockPlatformDevice(bar0_size));
        reg_io = std::unique_ptr<RegisterIo>(new RegisterIo(MockMmio::Create(bar0_size)));
        gtt = std::unique_ptr<Gtt>(new Gtt(this));

        bool ret = gtt->Init(gtt_size, platform_device.get());
        EXPECT_EQ(ret, true);

        uint32_t pat_index_low = reg_io->Read32(registers::PatIndex::kOffsetLow);
        EXPECT_EQ(pat_index_low, 4u);

        uint32_t pat_index_high = reg_io->Read32(registers::PatIndex::kOffsetHigh);
        EXPECT_EQ(pat_index_high, 0u);

        uint64_t scratch_bus_addr;
        ret = TestGtt::scratch_buffer(gtt.get())->MapPageBus(0, &scratch_bus_addr);
        EXPECT_TRUE(ret);

        // create some buffers
        std::vector<uint64_t> addr(2);
        std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

        buffer[0] = magma::PlatformBuffer::Create(1000);
        ret = gtt->Alloc(buffer[0]->size(), 0, &addr[0]);
        EXPECT_EQ(ret, true);

        buffer[1] = magma::PlatformBuffer::Create(10000);
        ret = gtt->Alloc(buffer[1]->size(), 0, &addr[1]);
        EXPECT_EQ(ret, true);

        // Try to insert without pinning
        ret = gtt->Insert(addr[0], buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE);
        EXPECT_EQ(ret, false);

        ret = buffer[0]->PinPages(0, buffer[0]->size() / PAGE_SIZE);
        EXPECT_EQ(ret, true);
        ret = buffer[1]->PinPages(0, buffer[1]->size() / PAGE_SIZE);
        EXPECT_EQ(ret, true);

        // Mismatch addr and buffer
        ret = gtt->Insert(addr[1], buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE);
        EXPECT_EQ(ret, false);

        // Totally bogus addr
        ret = gtt->Insert(0xdead1000, buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE);
        EXPECT_EQ(ret, false);

        // Correct
        ret = gtt->Insert(addr[0], buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE);
        EXPECT_EQ(ret, true);

        check_pte_entries(platform_device->mmio(), buffer[0].get(), addr[0], scratch_bus_addr,
                          CACHING_NONE);

        // Also correct
        ret = gtt->Insert(addr[1], buffer[1].get(), 0, buffer[1]->size(), CACHING_NONE);
        EXPECT_EQ(ret, true);

        check_pte_entries(platform_device->mmio(), buffer[1].get(), addr[1], scratch_bus_addr,
                          CACHING_NONE);

        // Bogus addr
        ret = gtt->Clear(0xdead1000);
        EXPECT_EQ(ret, false);

        // Cool
        ret = gtt->Clear(addr[1]);
        EXPECT_EQ(ret, true);

        check_pte_entries_clear(platform_device->mmio(), addr[1], buffer[1]->size(),
                                scratch_bus_addr);

        ret = gtt->Clear(addr[0]);
        EXPECT_EQ(ret, true);

        check_pte_entries_clear(platform_device->mmio(), addr[0], buffer[0]->size(),
                                scratch_bus_addr);

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

    RegisterIo* register_io() override { return reg_io.get(); }

    std::shared_ptr<MockPlatformDevice> platform_device;
    std::unique_ptr<RegisterIo> reg_io;
    std::unique_ptr<Gtt> gtt;
};

TEST(Gtt, Init)
{
    {
        TestDevice device;
        device.Init(3);
    }
    {
        TestDevice device;
        device.Init(2);
    }
    {
        TestDevice device;
        device.Init(1);
    }
}

TEST(Gtt, Insert)
{
    {
        TestDevice device;
        device.Insert();
    }
}

} // namespace
