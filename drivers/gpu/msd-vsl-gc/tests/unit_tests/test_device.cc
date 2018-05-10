// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/msd_vsl_device.h"
#include "helper/platform_device_helper.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>

// These tests are unit testing the functionality of MsdVslDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
class TestMsdVslDevice {
public:
    static void CreateAndDestroy()
    {
        std::unique_ptr<MsdVslDevice> device = MsdVslDevice::Create(GetTestDeviceHandle());
        EXPECT_NE(device, nullptr);
    }

    static void DeviceId()
    {
        std::unique_ptr<MsdVslDevice> device = MsdVslDevice::Create(GetTestDeviceHandle());
        ASSERT_NE(device, nullptr);
        EXPECT_EQ(0x7000u, device->device_id());
    }

    static void FetchEngineDma()
    {
        constexpr uint32_t kPageCount = 1;

        std::unique_ptr<MsdVslDevice> device = MsdVslDevice::Create(GetTestDeviceHandle());
        ASSERT_NE(device, nullptr);

        EXPECT_TRUE(device->IsIdle());

        std::unique_ptr<magma::PlatformBuffer> buffer =
            magma::PlatformBuffer::Create(PAGE_SIZE * kPageCount, "test");
        ASSERT_NE(buffer, nullptr);

        auto bus_mapping = device->bus_mapper()->MapPageRangeBus(buffer.get(), 0, kPageCount);
        ASSERT_NE(bus_mapping, nullptr);

        uint32_t length = 0;
        {
            uint32_t* cmd_ptr;
            ASSERT_TRUE(buffer->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));

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
    }
};

TEST(MsdVslDevice, CreateAndDestroy) { TestMsdVslDevice::CreateAndDestroy(); }

TEST(MsdVslDevice, DeviceId) { TestMsdVslDevice::DeviceId(); }

TEST(MsdVslDevice, FetchEngineDma) { TestMsdVslDevice::FetchEngineDma(); }
