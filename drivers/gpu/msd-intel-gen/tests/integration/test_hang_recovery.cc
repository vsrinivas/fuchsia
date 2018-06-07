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
        connection_ = magma_create_connection(fd(), MAGMA_CAPABILITY_RENDERING);
        DASSERT(connection_);

        magma_create_context(connection_, &context_id_);
    }

    ~TestConnection()
    {
        magma_release_context(connection_, context_id_);

        if (connection_)
            magma_release_connection(connection_);
    }

    enum How { NORMAL, FAULT, HANG };

    static constexpr bool kUseGlobalGtt = false;

    void SubmitCommandBuffer(How how)
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size;
        magma_buffer_t batch_buffer;

        ASSERT_EQ(magma_create_buffer(connection_, PAGE_SIZE, &size, &batch_buffer), 0);
        void* vaddr;
        ASSERT_EQ(0, magma_map(connection_, batch_buffer, &vaddr));

        ASSERT_TRUE(InitBatchBuffer(vaddr, size, how == HANG));

        magma_buffer_t command_buffer;
        ASSERT_EQ(magma_create_command_buffer(connection_, PAGE_SIZE, &command_buffer), 0);
        EXPECT_TRUE(InitCommandBuffer(command_buffer, batch_buffer, size, how == FAULT));
        magma_submit_command_buffer(connection_, command_buffer, context_id_);

        magma::InflightList list(connection_);

        switch (how) {
            case NORMAL:
                EXPECT_TRUE(list.WaitForCompletion(100));
                EXPECT_EQ(MAGMA_STATUS_OK, magma_get_error(connection_));
                EXPECT_EQ(kValue, reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1]);
                break;
            case FAULT:
                // Intel won't actually fault because bad gpu addresses are valid
                EXPECT_TRUE(list.WaitForCompletion(1200));
                EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, magma_get_error(connection_));
                EXPECT_EQ(0xdeadbeef, reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1]);
                break;
            case HANG:
                EXPECT_TRUE(list.WaitForCompletion(1200));
                EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, magma_get_error(connection_));
                EXPECT_EQ(kValue, reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1]);
                break;
        }

        EXPECT_EQ(magma_unmap(connection_, batch_buffer), 0);

        magma_release_buffer(connection_, batch_buffer);
    }

    bool InitBatchBuffer(void* vaddr, uint64_t size, bool hang)
    {
        memset(vaddr, 0, size);

        // store dword
        reinterpret_cast<uint32_t*>(vaddr)[0] =
            (0x20 << 23) | (4 - 2) | (kUseGlobalGtt ? 1 << 22 : 0);
        reinterpret_cast<uint32_t*>(vaddr)[1] =
            0x1000000; // gpu address - overwritten by relocation (or not)
        reinterpret_cast<uint32_t*>(vaddr)[2] = 0;
        reinterpret_cast<uint32_t*>(vaddr)[3] = kValue;

        // wait for semaphore - proceed if dword at given address > dword given
        reinterpret_cast<uint32_t*>(vaddr)[4] =
            (0x1C << 23) | (4 - 2) | (kUseGlobalGtt ? 1 << 22 : 0);
        reinterpret_cast<uint32_t*>(vaddr)[5] = hang ? ~0 : 0;
        reinterpret_cast<uint32_t*>(vaddr)[6] =
            0x1000000; // gpu address - overwritten by relocation (or not)
        reinterpret_cast<uint32_t*>(vaddr)[7] = 0;

        reinterpret_cast<uint32_t*>(vaddr)[8] = 0xA << 23;
        reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1] = 0xdeadbeef;

        return true;
    }

    bool InitCommandBuffer(magma_buffer_t buffer, magma_buffer_t batch_buffer,
                           uint64_t batch_buffer_length, bool fault)
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
        exec_resource->num_relocations = fault ? 0 : 2;
        exec_resource->offset = 0;
        exec_resource->length = batch_buffer_length;

        auto reloc = reinterpret_cast<struct magma_system_relocation_entry*>(exec_resource + 1);
        reloc->offset = 1 * 4;
        reloc->target_resource_index = 0;
        reloc->target_offset = batch_buffer_length - 4;

        reloc++;
        reloc->offset = 6 * 4;
        reloc->target_resource_index = 0;
        reloc->target_offset = batch_buffer_length - 4;

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

private:
    magma_connection_t* connection_;
    uint32_t context_id_;
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

TEST(HangRecovery, Stress) { TestConnection::Stress(1000); }
