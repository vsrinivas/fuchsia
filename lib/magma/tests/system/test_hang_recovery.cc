// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <thread>

#include "magma_system.h"
#include "magma_util/macros.h"
#include "gtest/gtest.h"

namespace {

constexpr uint32_t kValue = 0xabcddcba;

class TestBase {
public:
    TestBase() { fd_ = open("/dev/class/display/000", O_RDONLY); }

    int fd() { return fd_; }

    ~TestBase() { close(fd_); }

    void GetDeviceId() { EXPECT_NE(magma_system_get_device_id(fd_), 0u); }

private:
    int fd_;
};

class TestConnection : public TestBase {
public:
    TestConnection()
    {
        connection_ = magma_system_open(fd(), MAGMA_SYSTEM_CAPABILITY_RENDERING);
        DASSERT(connection_);

        magma_system_create_context(connection_, &context_id_);
    }

    ~TestConnection()
    {
        magma_system_destroy_context(connection_, context_id_);

        if (connection_)
            magma_system_close(connection_);
    }

    void SubmitCommandBuffer(bool bogus)
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size;
        magma_buffer_t batch_buffer, command_buffer;

        ASSERT_EQ(magma_system_alloc(connection_, PAGE_SIZE, &size, &batch_buffer), 0);
        ASSERT_EQ(magma_system_alloc(connection_, PAGE_SIZE, &size, &command_buffer), 0);

        void* vaddr;
        ASSERT_EQ(0, magma_system_map(connection_, batch_buffer, &vaddr));

        EXPECT_TRUE(InitBatchBuffer(vaddr, size));
        EXPECT_TRUE(InitCommandBuffer(command_buffer, batch_buffer, size, bogus));

        magma_system_submit_command_buffer(connection_, command_buffer, context_id_);

        // TODO(MA-129) - wait_rendering should return an error
        magma_system_wait_rendering(connection_, batch_buffer);

        if (bogus) {
            magma_status_t status = magma_system_get_error(connection_);
            // Wait rendering can't pass back a proper error yet
            EXPECT_TRUE(status == MAGMA_STATUS_CONNECTION_LOST ||
                        status == MAGMA_STATUS_INTERNAL_ERROR);
            EXPECT_EQ(0xdeadbeef, reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1]);
        } else {
            EXPECT_EQ(MAGMA_STATUS_OK, magma_system_get_error(connection_));
            EXPECT_EQ(kValue, reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1]);
        }

        EXPECT_EQ(magma_system_unmap(connection_, batch_buffer), 0);

        magma_system_free(connection_, batch_buffer);
        magma_system_free(connection_, command_buffer);
    }

    bool InitBatchBuffer(void* vaddr, uint64_t size)
    {
        if ((magma_system_get_device_id(fd()) >> 8) != 0x19)
            return DRETF(false, "not an intel gen9 device");

        memset(vaddr, 0, size);

        reinterpret_cast<uint32_t*>(vaddr)[0] = (0x20 << 23) | (4 - 2) | (1 << 22); // gtt
        reinterpret_cast<uint32_t*>(vaddr)[1] =
            0x1000000; // gpu address - overwritten by relocation (or not)
        reinterpret_cast<uint32_t*>(vaddr)[2] = 0;
        reinterpret_cast<uint32_t*>(vaddr)[3] = kValue;
        reinterpret_cast<uint32_t*>(vaddr)[4] = 0xA << 23;
        reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1] = 0xdeadbeef;

        return true;
    }

    bool InitCommandBuffer(magma_buffer_t buffer, magma_buffer_t batch_buffer,
                           uint64_t batch_buffer_length, bool bogus)
    {
        void* vaddr;
        if (magma_system_map(connection_, buffer, &vaddr) != 0)
            return DRETF(false, "couldn't map command buffer");

        auto command_buffer = reinterpret_cast<struct magma_system_command_buffer*>(vaddr);
        command_buffer->batch_buffer_resource_index = 0;
        command_buffer->batch_start_offset = 0;
        command_buffer->num_resources = 1;

        auto exec_resource =
            reinterpret_cast<struct magma_system_exec_resource*>(command_buffer + 1);
        exec_resource->buffer_id = magma_system_get_buffer_id(batch_buffer);
        exec_resource->num_relocations = bogus ? 0 : 1;
        exec_resource->offset = 0;
        exec_resource->length = batch_buffer_length;

        auto reloc = reinterpret_cast<struct magma_system_relocation_entry*>(exec_resource + 1);
        reloc->offset = 0x4;
        reloc->target_resource_index = 0;
        reloc->target_offset = batch_buffer_length - 4;
        reloc->read_domains_bitfield = 0;
        reloc->write_domains_bitfield = 0;

        EXPECT_EQ(magma_system_unmap(connection_, buffer), 0);

        return true;
    }

private:
    magma_system_connection* connection_;
    uint32_t context_id_;
};

} // namespace

TEST(HangRecovery, One)
{
    std::unique_ptr<TestConnection> test;
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(false);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(true);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(false);
}

TEST(HangRecovery, Two)
{
    std::thread happy([] {
        std::unique_ptr<TestConnection> test(new TestConnection());
        for (uint32_t count = 0; count < 100; count++) {
            test->SubmitCommandBuffer(false);
        }
    });

    std::thread sad([] {
        std::unique_ptr<TestConnection> test(new TestConnection());
        for (uint32_t count = 0; count < 100; count++) {
            if (count % 2 == 0) {
                test->SubmitCommandBuffer(false);
            } else {
                test->SubmitCommandBuffer(true);
                test.reset(new TestConnection());
            }
        }
    });

    happy.join();
    sad.join();
}
