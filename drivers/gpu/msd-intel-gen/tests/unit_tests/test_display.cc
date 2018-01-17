// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/msd_intel_device_core.h"
#include "helper/platform_device_helper.h"
#include "magma_util/semaphore_port.h"
#include "gtest/gtest.h"

class TestDisplay {
public:
    static void Flip(uint32_t num_buffers, uint32_t num_frames)
    {
        auto device = reinterpret_cast<MsdIntelDeviceCore*>(TestPlatformPciDevice::GetCoreDevice());
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

        for (uint32_t frame = 0; frame < num_frames; frame++) {
            uint32_t buffer_index = frame % buffers.size();

            uint32_t handle;
            EXPECT_TRUE(buffers[buffer_index]->platform_buffer()->duplicate_handle(&handle));

            device->PresentBuffer(handle, &image_desc, {}, {signal_semaphore},
                                  [frame](magma_status_t status, uint64_t vblank_time_ns) {
                                      static uint32_t callback_frame = 0;
                                      static uint64_t last_time_ns = 0;
                                      DLOG("present callback status %d frame %u ns %lu", status,
                                           frame, vblank_time_ns);
                                      EXPECT_EQ(status, MAGMA_STATUS_OK);
                                      EXPECT_EQ(callback_frame++, frame);
                                      EXPECT_GT(vblank_time_ns, last_time_ns);
                                      last_time_ns = vblank_time_ns;
                                  });
            if (frame > 0)
                EXPECT_TRUE(signal_semaphore->Wait(1000));
        }
    }

    TestDisplay(MsdIntelDeviceCore* device, std::unique_ptr<magma::SemaphorePort> semaphore_port)
        : device_(std::move(device)), semaphore_port_(std::move(semaphore_port))
    {
    }

    static std::unique_ptr<TestDisplay> Create()
    {
        auto core_device =
            reinterpret_cast<MsdIntelDeviceCore*>(TestPlatformPciDevice::GetCoreDevice());
        if (!core_device)
            return DRETP(nullptr, "no core device");

        return std::make_unique<TestDisplay>(core_device, magma::SemaphorePort::Create());
    }

    void FlipSync(uint32_t num_buffers)
    {
        for (uint32_t i = 0; i < num_buffers; i++) {
            buffers_.push_back(MsdIntelBuffer::Create(PAGE_SIZE, "test"));

            wait_semaphores_.push_back(
                std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create()));
            signal_semaphores_.push_back(
                std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create()));

            wait_semaphores_copy_.push_back(wait_semaphores_[i]);

            auto callback = [this](magma::SemaphorePort::WaitSet* wait_set) {
                DLOG("callback semaphore %lu", wait_set->semaphore(0)->id());
                static uint32_t index;
                EXPECT_EQ(wait_set->semaphore_count(), 1u);
                // The wait semaphore is removed from the array when signalled
                EXPECT_EQ(this->wait_semaphores_copy_[index], nullptr);
                EXPECT_EQ(wait_set->semaphore(0)->id(), this->signal_semaphores_[index]->id());
                index++;
            };

            EXPECT_TRUE(semaphore_port_->AddWaitSet(std::make_unique<magma::SemaphorePort::WaitSet>(
                callback,
                std::vector<std::shared_ptr<magma::PlatformSemaphore>>{signal_semaphores_[i]})));
        }

        std::thread wait_thread([this] {
            for (uint32_t i = 0; i < this->buffers_.size(); i++) {
                EXPECT_EQ(MAGMA_STATUS_OK, this->semaphore_port_->WaitOne().get());
                DLOG("WaitOne returned %u", i);
            }
        });

        magma_system_image_descriptor image_desc{MAGMA_IMAGE_TILING_LINEAR};

        auto follow_on = std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "test"));

        for (uint32_t i = 0; i < buffers_.size(); i++) {
            DLOG("flipping wait semaphore %lu signal semaphore %lu",
                 this->wait_semaphores_[i]->id(), this->signal_semaphores_[i]->id());
            uint32_t handle;
            EXPECT_TRUE(buffers_[i]->platform_buffer()->duplicate_handle(&handle));

            device_->PresentBuffer(
                handle, &image_desc,
                std::vector<std::shared_ptr<magma::PlatformSemaphore>>{this->wait_semaphores_[i]},
                std::vector<std::shared_ptr<magma::PlatformSemaphore>>{this->signal_semaphores_[i]},
                nullptr);

            // Flip another buffer to push the previous one off the display
            if (i > 0) {
                EXPECT_TRUE(follow_on->platform_buffer()->duplicate_handle(&handle));

                device_->PresentBuffer(handle, &image_desc, {}, {}, nullptr);
            }

            // Delay must be long enough to flush out buffer that's been erroneously
            // advanced before its wait semaphore was signaled
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            auto semaphore = this->wait_semaphores_copy_[i];
            DLOG("signalling wait semaphore %lu", semaphore->id());
            wait_semaphores_copy_[i] = nullptr;
            semaphore->Signal();
        }

        uint32_t handle;
        EXPECT_TRUE(buffers_[0]->platform_buffer()->duplicate_handle(&handle));

        // Extra flip to release the last buffer
        device_->PresentBuffer(handle, &image_desc,
                               std::vector<std::shared_ptr<magma::PlatformSemaphore>>{},
                               std::vector<std::shared_ptr<magma::PlatformSemaphore>>{}, nullptr);

        DLOG("joining wait thread");
        wait_thread.join();
    }

private:
    MsdIntelDeviceCore* device_;
    std::vector<std::shared_ptr<MsdIntelBuffer>> buffers_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores_copy_;
    std::unique_ptr<magma::SemaphorePort> semaphore_port_;
};

TEST(Display, DoubleBufferFlip) { TestDisplay::Flip(2, 10); }

TEST(Display, FlipSync)
{
    auto test = TestDisplay::Create();
    ASSERT_NE(test, nullptr);
    test->FlipSync(100);
}
