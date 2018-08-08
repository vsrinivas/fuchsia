// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "cmdstream_fuchsia.h"
int etnaviv_cl_test_gc7000(int argc, char* argv[]);
}

#include "garnet/drivers/gpu/msd-vsl-gc/src/address_space.h"
#include "garnet/drivers/gpu/msd-vsl-gc/src/msd_vsl_device.h"
#include "helper/platform_device_helper.h"
#include "magma_util/macros.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>

TEST(MsdVslDevice, MemoryWrite) { EXPECT_EQ(0, etnaviv_cl_test_gc7000(0, nullptr)); }

class TestMsdVslDevice : public drm_test_info {
public:
    bool Init()
    {
        DLOG("init begin");

        device_.test = command_stream_.test = this;

        this->dev = &device_;
        this->stream = &command_stream_;

        device_.msd_vsl_device = MsdVslDevice::Create(GetTestDeviceHandle());
        if (!device_.msd_vsl_device)
            return DRETF(false, "no test device");

        if (!device_.msd_vsl_device->IsIdle())
            return DRETF(false, "device not idle");

        address_space_owner_ =
            std::make_unique<AddressSpaceOwner>(device_.msd_vsl_device->bus_mapper());
        address_space_ = AddressSpace::Create(address_space_owner_.get());
        if (!address_space_)
            return DRETF(false, "failed to create address space");

        static constexpr uint32_t kAddressSpaceIndex = 1;

        device_.msd_vsl_device->page_table_arrays()->AssignAddressSpace(kAddressSpaceIndex,
                                                                        address_space_.get());

        if (!LoadAddressSpace(device_.msd_vsl_device.get(), kAddressSpaceIndex))
            return DRETF(false, "failed to load address space");

        DLOG("address space loaded");

        command_stream_.etna_buffer = static_cast<EtnaBuffer*>(
            etna_bo_new(this->dev, PAGE_SIZE, DRM_ETNA_GEM_CACHE_UNCACHED));
        if (!command_stream_.etna_buffer)
            return DRETF(false, "failed to get command stream buffer");

        if (!command_stream_.etna_buffer->buffer->MapCpu(
                reinterpret_cast<void**>(&command_stream_.cmd_ptr)))
            return DRETF(false, "failed to map cmd_ptr");

        DLOG("init complete");

        return true;
    }

    static bool LoadAddressSpace(MsdVslDevice* device, uint32_t index)
    {
        // Switch to the address space with a command buffer.
        static constexpr uint32_t kPageCount = 1;

        std::unique_ptr<magma::PlatformBuffer> buffer =
            magma::PlatformBuffer::Create(PAGE_SIZE * kPageCount, "test");
        if (!buffer)
            return DRETF(false, "Couldn't create buffer");

        auto bus_mapping = device->bus_mapper()->MapPageRangeBus(buffer.get(), 0, kPageCount);
        if (!bus_mapping)
            return DRETF(false, "couldn't create bus mapping");

        uint32_t length = 0;
        {
            uint32_t* cmd_ptr;
            if (!buffer->MapCpu(reinterpret_cast<void**>(&cmd_ptr)))
                return DRETF(false, "failed to map command buffer");

            cmd_ptr[length++] =
                (1 << 27)                                                  // load state
                | (1 << 16)                                                // count
                | (registers::MmuPageTableArrayConfig::Get().addr() >> 2); // register to be written
            cmd_ptr[length++] = index;
            cmd_ptr[length++] = (2 << 27); // end

            EXPECT_TRUE(buffer->UnmapCpu());
            EXPECT_TRUE(buffer->CleanCache(0, PAGE_SIZE * kPageCount, false));
        }

        length *= sizeof(uint32_t);
        uint16_t prefetch = 0;

        EXPECT_TRUE(device->SubmitCommandBufferNoMmu(bus_mapping->Get()[0], length, &prefetch));
        EXPECT_EQ(magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t), prefetch);

        auto start = std::chrono::high_resolution_clock::now();
        while (!device->IsIdle() && std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::high_resolution_clock::now() - start)
                                            .count() < 1000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        EXPECT_TRUE(device->IsIdle());

        auto dma_addr = registers::DmaAddress::Get().ReadFrom(device->register_io());
        EXPECT_EQ(dma_addr.reg_value(), bus_mapping->Get()[0] + prefetch * sizeof(uint64_t));

        device->page_table_arrays()->Enable(device->register_io(), true);

        return true;
    }

    struct EtnaDevice : public etna_dev {
        std::unique_ptr<MsdVslDevice> msd_vsl_device;
        TestMsdVslDevice* test = nullptr;
    };

    struct EtnaBuffer : public etna_bo {
        std::unique_ptr<magma::PlatformBuffer> buffer;
        std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping;
        uint32_t gpu_addr = 0xFAFAFAFA;
    };

    struct EtnaCommandStream : public etna_cmd_stream {
        EtnaBuffer* etna_buffer = nullptr;
        uint32_t* cmd_ptr = nullptr;
        uint32_t index = 0;
        TestMsdVslDevice* test = nullptr;
    };

    MsdVslDevice* device() { return device_.msd_vsl_device.get(); }

    magma::PlatformBusMapper* bus_mapper() { return device_.msd_vsl_device->bus_mapper(); }

    magma::RegisterIo* register_io() { return device_.msd_vsl_device->register_io(); }

    AddressSpace* address_space() { return address_space_.get(); }

    bool SubmitCommandBuffer(uint32_t gpu_addr, uint32_t length, uint16_t* prefetch_out)
    {
        return device_.msd_vsl_device->SubmitCommandBuffer(gpu_addr, length, prefetch_out);
    }

    uint32_t next_gpu_addr(uint32_t size)
    {
        uint32_t next = next_gpu_addr_;
        next_gpu_addr_ += size;
        return next;
    }

private:
    class AddressSpaceOwner : public AddressSpace::Owner {
    public:
        AddressSpaceOwner(magma::PlatformBusMapper* bus_mapper) : bus_mapper_(bus_mapper) {}
        virtual ~AddressSpaceOwner() = default;

        magma::PlatformBusMapper* bus_mapper() override { return bus_mapper_; }

    private:
        magma::PlatformBusMapper* bus_mapper_;
    };

    EtnaDevice device_;
    EtnaCommandStream command_stream_;

    std::unique_ptr<AddressSpaceOwner> address_space_owner_;
    std::unique_ptr<AddressSpace> address_space_;
    uint32_t next_gpu_addr_ = 0x10000;
};

struct drm_test_info* drm_test_setup(int argc, char** argv)
{
    auto test_info = std::make_unique<TestMsdVslDevice>();
    if (!test_info->Init())
        return DRETP(nullptr, "failed to init test");
    return test_info.release();
}

void drm_test_teardown(struct drm_test_info* info) { delete static_cast<TestMsdVslDevice*>(info); }

void etna_set_state(struct etna_cmd_stream* stream, uint32_t address, uint32_t value)
{
    DLOG("set state 0x%x 0x%x", address, value);
    auto cmd_stream = static_cast<TestMsdVslDevice::EtnaCommandStream*>(stream);

    cmd_stream->cmd_ptr[cmd_stream->index++] = (1 << 27)         // load state
                                               | (1 << 16)       // count
                                               | (address >> 2); // register to be written
    cmd_stream->cmd_ptr[cmd_stream->index++] = value;
}

void etna_set_state_from_bo(struct etna_cmd_stream* stream, uint32_t address, struct etna_bo* bo,
                            uint32_t reloc_flags)
{
    DLOG("set state from bo 0x%x gpu_addr 0x%x", address,
         static_cast<TestMsdVslDevice::EtnaBuffer*>(bo)->gpu_addr);
    auto cmd_stream = static_cast<TestMsdVslDevice::EtnaCommandStream*>(stream);

    cmd_stream->cmd_ptr[cmd_stream->index++] = (1 << 27)         // load state
                                               | (1 << 16)       // count
                                               | (address >> 2); // register to be written
    cmd_stream->cmd_ptr[cmd_stream->index++] =
        static_cast<TestMsdVslDevice::EtnaBuffer*>(bo)->gpu_addr;
}

void etna_stall(struct etna_cmd_stream* stream, uint32_t from, uint32_t to)
{
    DLOG("stall %u %u", from, to);
    auto cmd_stream = static_cast<TestMsdVslDevice::EtnaCommandStream*>(stream);

    etna_set_state(stream, 0x00003808, (from & 0x1f) | ((to << 8) & 0x1f00));

    if (from == 1) { // FE
        cmd_stream->cmd_ptr[cmd_stream->index++] = 0x48000000;
        cmd_stream->cmd_ptr[cmd_stream->index++] = (from & 0x1f) | ((to << 8) & 0x1f00);
    } else {
        DASSERT(false);
    }
}

// Create a buffer and map it into the gpu address space.
struct etna_bo* etna_bo_new(void* dev, uint32_t size, uint32_t flags)
{
    DLOG("bo new size %u flags 0x%x", size, flags);
    auto etna_buffer = std::make_unique<TestMsdVslDevice::EtnaBuffer>();

    etna_buffer->buffer = magma::PlatformBuffer::Create(size, "EtnaBuffer");
    if (!etna_buffer->buffer)
        return DRETP(nullptr, "failed to alloc buffer size %u", size);

    if (flags & DRM_ETNA_GEM_CACHE_UNCACHED)
        etna_buffer->buffer->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED);

    auto etna_device = static_cast<TestMsdVslDevice::EtnaDevice*>(dev);
    uint32_t page_count = etna_buffer->buffer->size() / PAGE_SIZE;

    etna_buffer->bus_mapping =
        etna_device->test->bus_mapper()->MapPageRangeBus(etna_buffer->buffer.get(), 0, page_count);
    if (!etna_buffer->bus_mapping)
        return DRETP(nullptr, "failed to bus map buffer");

    etna_buffer->gpu_addr = etna_device->test->next_gpu_addr(etna_buffer->buffer->size());

    if (!etna_device->test->address_space()->Insert(etna_buffer->gpu_addr,
                                                    etna_buffer->bus_mapping.get(), page_count))
        return DRETP(nullptr, "couldn't insert into address space");

    return etna_buffer.release();
}

void* etna_bo_map(struct etna_bo* bo)
{
    DLOG("bo map %p", bo);
    void* addr;
    if (!static_cast<TestMsdVslDevice::EtnaBuffer*>(bo)->buffer->MapCpu(&addr))
        return DRETP(nullptr, "Failed to map etna buffer");
    DLOG("bo map returning %p", addr);
    return addr;
}

void etna_cmd_stream_finish(struct etna_cmd_stream* stream)
{
    auto cmd_stream = static_cast<TestMsdVslDevice::EtnaCommandStream*>(stream);
    cmd_stream->cmd_ptr[cmd_stream->index++] = (2 << 27); // end

    uint32_t length = cmd_stream->index * sizeof(uint32_t);
    uint16_t prefetch = 0;

    DLOG("etna_cmd_stream_finish length %u", length);

    EXPECT_TRUE(cmd_stream->test->SubmitCommandBuffer(cmd_stream->etna_buffer->gpu_addr, length,
                                                      &prefetch));
    EXPECT_EQ(magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t), prefetch);

    auto start = std::chrono::high_resolution_clock::now();
    while (!cmd_stream->test->device()->IsIdle() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::high_resolution_clock::now() - start)
                   .count() < 1000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    {
        auto reg = registers::IdleState::Get().ReadFrom(cmd_stream->test->register_io());
        EXPECT_EQ(0x7FFFFFFFu, reg.reg_value());
    }
    {
        auto dma_addr = registers::DmaAddress::Get().ReadFrom(cmd_stream->test->register_io());
        EXPECT_EQ(dma_addr.reg_value(),
                  cmd_stream->etna_buffer->gpu_addr + prefetch * sizeof(uint64_t));
        DLOG("dma_addr 0x%x", dma_addr.reg_value());
    }

    DLOG("execution took %lld ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::high_resolution_clock::now() - start)
                                       .count());
    {
        auto reg = registers::MmuSecureStatus::Get().ReadFrom(cmd_stream->test->register_io());
        EXPECT_EQ(0u, reg.reg_value());
    }
    {
        auto reg =
            registers::MmuSecureExceptionAddress::Get().ReadFrom(cmd_stream->test->register_io());
        EXPECT_EQ(0u, reg.reg_value());
    }
}
