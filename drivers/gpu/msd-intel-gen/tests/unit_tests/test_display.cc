// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "msd_intel_device.h"
#include "msd_intel_driver.h"
#include "gtest/gtest.h"

class TestDisplay {
public:
    void Flip(uint32_t num_buffers, uint32_t num_frames)
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device(
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), true));
        ASSERT_NE(device, nullptr);

        uint32_t buffer_size = 2160 * 1440 * 4;

        std::vector<std::shared_ptr<MsdIntelBuffer>> buffers;

        for (uint32_t i = 0; i < num_buffers; i++) {
            auto buffer = MsdIntelBuffer::Create(buffer_size, "test");
            ASSERT_NE(buffer, nullptr);

            void* vaddr;
            ASSERT_TRUE(buffer->platform_buffer()->MapCpu(&vaddr));

            const uint32_t pixel = (0xFF << (i * 8)) | 0xFF000000;

            for (uint32_t i = 0; i < 2160 * 1440; i++) {
                reinterpret_cast<uint32_t*>(vaddr)[i] = pixel;
            }

            EXPECT_TRUE(buffer->platform_buffer()->UnmapCpu());

            buffers.push_back(std::move(buffer));
        }

        magma_system_image_descriptor image_desc{MAGMA_IMAGE_TILING_LINEAR};

        auto signal_semaphore =
            std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());

        std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
        std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
        signal_semaphores.push_back(signal_semaphore);

        for (uint32_t frame = 0; frame < num_frames; frame++) {
            uint32_t buffer_index = frame % buffers.size();
            device->Flip(buffers[buffer_index], &image_desc, wait_semaphores, signal_semaphores);
            if (frame > 0)
                EXPECT_TRUE(signal_semaphore->Wait(1000));
        }
    }
};

TEST(Display, DoubleBufferFlip)
{
    TestDisplay test;
    test.Flip(2, 10);
}
