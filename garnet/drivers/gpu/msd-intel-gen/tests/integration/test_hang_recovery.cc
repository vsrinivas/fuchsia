// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <fcntl.h>
#include <thread>

#include "magma.h"
#include "magma_util/dlog.h"
#include "magma_util/inflight_list.h"
#include "magma_util/macros.h"
#include "msd_intel_gen_query.h"
#include "gtest/gtest.h"

namespace {

constexpr uint32_t kValue = 0xabcddcba;

class TestBase {
public:
    TestBase() { fd_ = open("/dev/class/gpu/000", O_RDONLY); }

    int fd() { return fd_; }

    ~TestBase() { close(fd_); }

private:
    int fd_;
};

class TestConnection : public TestBase {
public:
    TestConnection()
    {
        magma_create_connection(fd(), &connection_);
        DASSERT(connection_);

        magma_status_t status =
            magma_query(fd(), kMsdIntelGenQueryExtraPageCount, &extra_page_count_);
        if (status != MAGMA_STATUS_OK) {
            DLOG("Failed to query kMsdIntelGenQueryExtraPageCount: %d", status);
            extra_page_count_ = 0;
        }

        magma_create_context(connection_, &context_id_);
    }

    ~TestConnection()
    {
        if (connection_) {
            magma_release_context(connection_, context_id_);
            magma_release_connection(connection_);
        }
    }

    enum How { NORMAL, FAULT, HANG };

    static constexpr bool kUseGlobalGtt = false;
    static constexpr int64_t kOneSecondInNs = 1000000000;
    static constexpr uint64_t kUnmappedBufferGpuAddress = 0x1000000; // arbitrary

    void SubmitCommandBuffer(How how)
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t buffer_size;
        magma_buffer_t batch_buffer;

        ASSERT_EQ(magma_create_buffer(connection_, PAGE_SIZE, &buffer_size, &batch_buffer), 0);
        void* vaddr;
        ASSERT_EQ(MAGMA_STATUS_OK, magma_map(connection_, batch_buffer, &vaddr));

        magma_map_buffer_gpu(connection_, batch_buffer, 0, 1, gpu_addr_, 0);

        // Write to the last dword
        InitBatchBuffer(vaddr, buffer_size, how == HANG,
                                    how == FAULT ? kUnmappedBufferGpuAddress
                                                 : gpu_addr_ + buffer_size - sizeof(uint32_t));

        // Increment gpu address for next iteration
        gpu_addr_ += (1 + extra_page_count_) * PAGE_SIZE;

        magma_buffer_t command_buffer;
        ASSERT_EQ(magma_create_command_buffer(connection_, PAGE_SIZE, &command_buffer),
                  MAGMA_STATUS_OK);
        EXPECT_TRUE(InitCommandBuffer(command_buffer, batch_buffer, buffer_size));
        magma_submit_command_buffer(connection_, command_buffer, context_id_);

        magma::InflightList list;

        switch (how) {
            case NORMAL:
                EXPECT_TRUE(list.WaitForCompletion(connection_, kOneSecondInNs));
                EXPECT_EQ(MAGMA_STATUS_OK, magma_get_error(connection_));
                EXPECT_EQ(kValue, reinterpret_cast<uint32_t*>(vaddr)[buffer_size / 4 - 1]);
                break;
            case FAULT: {
                // Intel won't actually fault because bad gpu addresses are valid
                auto start = std::chrono::steady_clock::now();
                while (std::chrono::duration<double, std::milli>(
                           std::chrono::high_resolution_clock::now() - start)
                           .count() < 2000) {
                    if (magma_get_error(connection_) == MAGMA_STATUS_CONNECTION_LOST) {
                        break;
                    }
                }
                EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, magma_get_error(connection_));
                EXPECT_TRUE(list.WaitForCompletion(connection_, kOneSecondInNs));
                EXPECT_EQ(0xdeadbeef, reinterpret_cast<uint32_t*>(vaddr)[buffer_size / 4 - 1]);
                break;
            }
            case HANG: {
                auto start = std::chrono::steady_clock::now();
                while (std::chrono::duration<double, std::milli>(
                           std::chrono::high_resolution_clock::now() - start)
                           .count() < 2000) {
                    if (magma_get_error(connection_) == MAGMA_STATUS_CONNECTION_LOST) {
                        break;
                    }
                }
                EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, magma_get_error(connection_));
                EXPECT_TRUE(list.WaitForCompletion(connection_, kOneSecondInNs));
                EXPECT_EQ(kValue, reinterpret_cast<uint32_t*>(vaddr)[buffer_size / 4 - 1]);
                break;
            }
        }

        EXPECT_EQ(magma_unmap(connection_, batch_buffer), 0);

        magma_release_buffer(connection_, batch_buffer);
    }

    void InitBatchBuffer(void* vaddr, uint64_t size, bool hang, uint64_t gpu_addr)
    {
        memset(vaddr, 0, size);

        constexpr uint32_t kStoreDwordOp = 0x20 << 23;
        constexpr uint32_t kStoreDwordCount = 4 - 2; // always -2
        reinterpret_cast<uint32_t*>(vaddr)[0] =
            kStoreDwordOp | kStoreDwordCount | (kUseGlobalGtt ? 1 << 22 : 0);
        reinterpret_cast<uint32_t*>(vaddr)[1] = gpu_addr;
        reinterpret_cast<uint32_t*>(vaddr)[2] = 0;
        reinterpret_cast<uint32_t*>(vaddr)[3] = kValue;

        constexpr uint32_t kWaitForSemaphoreOp = 0x1C << 23;
        constexpr uint32_t kWaitForSemaphoreCount = 4 - 2; // always -2
        // wait for semaphore - proceed if dword at given address > dword given
        reinterpret_cast<uint32_t*>(vaddr)[4] =
            kWaitForSemaphoreOp | kWaitForSemaphoreCount | (kUseGlobalGtt ? 1 << 22 : 0);
        reinterpret_cast<uint32_t*>(vaddr)[5] = hang ? ~0 : 0;
        reinterpret_cast<uint32_t*>(vaddr)[6] = gpu_addr;
        reinterpret_cast<uint32_t*>(vaddr)[7] = 0;

        constexpr uint32_t kEndBatchOp = 0xA << 23;
        reinterpret_cast<uint32_t*>(vaddr)[8] = kEndBatchOp;

        // initialize scratch memory location
        reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1] = 0xdeadbeef;
    }

    bool InitCommandBuffer(magma_buffer_t buffer, magma_buffer_t batch_buffer,
                           uint64_t batch_buffer_length)
    {
        void* vaddr;
        if (magma_map(connection_, buffer, &vaddr) != 0)
            return DRETF(false, "couldn't map command buffer");

        auto command_buffer = reinterpret_cast<struct magma_system_command_buffer*>(vaddr);
        command_buffer->batch_buffer_resource_index = 0;
        command_buffer->batch_start_offset = 0;
        command_buffer->num_resources = 1;

        auto exec_resource =
            reinterpret_cast<struct magma_system_exec_resource*>(command_buffer + 1);
        exec_resource->buffer_id = magma_get_buffer_id(batch_buffer);
        exec_resource->offset = 0;
        exec_resource->length = batch_buffer_length;

        EXPECT_EQ(magma_unmap(connection_, buffer), 0);

        return true;
    }

    static void Stress(uint32_t iterations)
    {
        for (uint32_t i = 0; i < iterations; i++) {
            DLOG("iteration %d/%d", i, iterations);
            std::thread happy([] {
                std::unique_ptr<TestConnection> test(new TestConnection());
                for (uint32_t count = 0; count < 100; count++) {
                    test->SubmitCommandBuffer(TestConnection::NORMAL);
                }
            });

            std::thread sad([] {
                std::unique_ptr<TestConnection> test(new TestConnection());
                for (uint32_t count = 0; count < 100; count++) {
                    if (count % 2 == 0) {
                        test->SubmitCommandBuffer(TestConnection::NORMAL);
                    } else if (count % 3 == 0) {
                        test->SubmitCommandBuffer(TestConnection::FAULT);
                        test.reset(new TestConnection());
                    } else {
                        test->SubmitCommandBuffer(TestConnection::HANG);
                        test.reset(new TestConnection());
                    }
                }
            });

            happy.join();
            sad.join();
        }
    }

    void SubmitAndDisconnect()
    {
        uint64_t size;
        magma_buffer_t batch_buffer;

        ASSERT_EQ(magma_create_buffer(connection_, PAGE_SIZE, &size, &batch_buffer), 0);
        void* vaddr;
        ASSERT_EQ(0, magma_map(connection_, batch_buffer, &vaddr));

        InitBatchBuffer(vaddr, size, true, kUnmappedBufferGpuAddress);

        magma_buffer_t command_buffer;
        ASSERT_EQ(magma_create_command_buffer(connection_, PAGE_SIZE, &command_buffer), 0);
        EXPECT_TRUE(InitCommandBuffer(command_buffer, batch_buffer, size));
        magma_submit_command_buffer(connection_, command_buffer, context_id_);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        magma_release_connection(connection_);
        connection_ = nullptr;
    }

private:
    magma_connection_t connection_;
    uint32_t context_id_;
    uint64_t extra_page_count_ = 0;
    uint64_t gpu_addr_ = 0;
};

} // namespace

TEST(HangRecovery, Test)
{
    std::unique_ptr<TestConnection> test;
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::NORMAL);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::FAULT);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::NORMAL);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::HANG);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::NORMAL);
}

TEST(HangRecovery, DISABLED_Stress) { TestConnection::Stress(1000); }

TEST(HangRecovery, SubmitAndDisconnect) { TestConnection().SubmitAndDisconnect(); }
