// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_connection.h"
#include "gtest/gtest.h"
#include <chrono>
#include <poll.h>
#include <thread>

namespace {
constexpr uint32_t kImmediateCommandSize = 4096 / 128;
}

class TestPlatformConnection {
public:
    static std::unique_ptr<TestPlatformConnection> Create();

    TestPlatformConnection(std::unique_ptr<magma::PlatformIpcConnection> ipc_connection,
                           std::thread ipc_thread)
        : ipc_connection_(std::move(ipc_connection)), ipc_thread_(std::move(ipc_thread))
    {
    }

    ~TestPlatformConnection()
    {
        ipc_connection_.reset();
        if (ipc_thread_.joinable())
            ipc_thread_.join();
        EXPECT_TRUE(test_complete);
    }

    void TestImportBuffer()
    {
        auto buf = magma::PlatformBuffer::Create(1, "test");
        test_buffer_id = buf->id();
        EXPECT_EQ(ipc_connection_->ImportBuffer(buf.get()), 0);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }
    void TestReleaseBuffer()
    {
        auto buf = magma::PlatformBuffer::Create(1, "test");
        test_buffer_id = buf->id();
        EXPECT_EQ(ipc_connection_->ImportBuffer(buf.get()), 0);
        EXPECT_EQ(ipc_connection_->ReleaseBuffer(test_buffer_id), 0);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }

    void TestImportObject()
    {
        auto semaphore = magma::PlatformSemaphore::Create();
        test_semaphore_id = semaphore->id();
        uint32_t handle;
        EXPECT_TRUE(semaphore->duplicate_handle(&handle));
        EXPECT_EQ(ipc_connection_->ImportObject(handle, magma::PlatformObject::SEMAPHORE), 0);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }
    void TestReleaseObject()
    {
        auto semaphore = magma::PlatformSemaphore::Create();
        test_semaphore_id = semaphore->id();
        uint32_t handle;
        EXPECT_TRUE(semaphore->duplicate_handle(&handle));
        EXPECT_EQ(ipc_connection_->ImportObject(handle, magma::PlatformObject::SEMAPHORE), 0);
        EXPECT_EQ(
            ipc_connection_->ReleaseObject(test_semaphore_id, magma::PlatformObject::SEMAPHORE), 0);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }

    void TestCreateContext()
    {
        uint32_t context_id;
        ipc_connection_->CreateContext(&context_id);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
        EXPECT_EQ(test_context_id, context_id);
    }
    void TestDestroyContext()
    {
        ipc_connection_->DestroyContext(test_context_id);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }

    void TestExecuteCommandBuffer()
    {
        auto buf = magma::PlatformBuffer::Create(1, "test");
        test_buffer_id = buf->id();
        uint32_t handle;
        EXPECT_TRUE(buf->duplicate_handle(&handle));
        ipc_connection_->ExecuteCommandBuffer(handle, test_context_id);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }

    void TestGetError()
    {
        EXPECT_EQ(ipc_connection_->GetError(), 0);
        test_complete = true;
    }

    void TestMapUnmapBuffer()
    {
        auto buf = magma::PlatformBuffer::Create(1, "test");
        test_buffer_id = buf->id();
        EXPECT_EQ(ipc_connection_->ImportBuffer(buf.get()), 0);
        EXPECT_EQ(ipc_connection_->MapBufferGpu(buf->id(), PAGE_SIZE * 1000, 1u, 2u, 5), 0);
        EXPECT_EQ(ipc_connection_->UnmapBufferGpu(buf->id(), PAGE_SIZE * 1000), 0);
        EXPECT_EQ(ipc_connection_->CommitBuffer(buf->id(), 1000, 2000), 0);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }

    void TestNotificationChannel()
    {
        pollfd pfd = {.fd = ipc_connection_->GetNotificationChannelFd(), .events = POLLIN};

        int poll_status = poll(&pfd, 1, 5000);
        EXPECT_EQ(1, poll_status);

        uint32_t out_data;
        uint64_t out_data_size;
        // Data was written when the channel was created, so it should be
        // available.
        magma_status_t status =
            ipc_connection_->ReadNotificationChannel(&out_data, sizeof(out_data), &out_data_size);
        EXPECT_EQ(MAGMA_STATUS_OK, status);
        EXPECT_EQ(sizeof(out_data), out_data_size);
        EXPECT_EQ(5u, out_data);

        // No more data to read.
        status =
            ipc_connection_->ReadNotificationChannel(&out_data, sizeof(out_data), &out_data_size);
        EXPECT_EQ(MAGMA_STATUS_OK, status);
        EXPECT_EQ(0u, out_data_size);
    }

    void TestExecuteImmediateCommands()
    {
        uint8_t commands_buffer[4096] = {};
        uint64_t semaphore_ids[]{0, 1, 2};
        magma_system_inline_command_buffer commands[128];
        static_assert(kImmediateCommandSize * 128 == sizeof(commands_buffer),
                      "Incorrect command size");
        for (size_t i = 0; i < 128; i++) {
            commands[i].data = commands_buffer;
            commands[i].size = kImmediateCommandSize;
            commands[i].semaphore_count = 3;
            commands[i].semaphores = semaphore_ids;
        }

        ipc_connection_->ExecuteImmediateCommands(test_context_id, 128, commands);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }

    static uint64_t test_buffer_id;
    static uint32_t test_context_id;
    static uint64_t test_semaphore_id;
    static magma_status_t test_error;
    static bool test_complete;
    static std::unique_ptr<magma::PlatformSemaphore> test_semaphore;

private:
    static void IpcThreadFunc(std::shared_ptr<magma::PlatformConnection> connection)
    {
        while (connection->HandleRequest())
            ;
    }

    std::unique_ptr<magma::PlatformIpcConnection> ipc_connection_;
    std::thread ipc_thread_;
};

uint64_t TestPlatformConnection::test_buffer_id;
uint64_t TestPlatformConnection::test_semaphore_id;
uint32_t TestPlatformConnection::test_context_id;
magma_status_t TestPlatformConnection::test_error;
bool TestPlatformConnection::test_complete;
std::unique_ptr<magma::PlatformSemaphore> TestPlatformConnection::test_semaphore;

class TestDelegate : public magma::PlatformConnection::Delegate {
public:
    bool ImportBuffer(uint32_t handle, uint64_t* buffer_id_out) override
    {
        auto buf = magma::PlatformBuffer::Import(handle);
        EXPECT_EQ(buf->id(), TestPlatformConnection::test_buffer_id);
        TestPlatformConnection::test_complete = true;
        return true;
    }
    bool ReleaseBuffer(uint64_t buffer_id) override
    {
        EXPECT_EQ(buffer_id, TestPlatformConnection::test_buffer_id);
        TestPlatformConnection::test_complete = true;
        return true;
    }

    bool ImportObject(uint32_t handle, magma::PlatformObject::Type object_type) override
    {
        auto semaphore = magma::PlatformSemaphore::Import(handle);
        EXPECT_EQ(semaphore->id(), TestPlatformConnection::test_semaphore_id);
        TestPlatformConnection::test_complete = true;
        return true;
    }
    bool ReleaseObject(uint64_t object_id, magma::PlatformObject::Type object_type) override
    {
        EXPECT_EQ(object_id, TestPlatformConnection::test_semaphore_id);
        TestPlatformConnection::test_complete = true;
        return true;
    }

    bool CreateContext(uint32_t context_id) override
    {
        TestPlatformConnection::test_context_id = context_id;
        TestPlatformConnection::test_complete = true;
        return true;
    }
    bool DestroyContext(uint32_t context_id) override
    {
        EXPECT_EQ(context_id, TestPlatformConnection::test_context_id);
        TestPlatformConnection::test_complete = true;
        return true;
    }

    magma::Status ExecuteCommandBuffer(uint32_t command_buffer_handle, uint32_t context_id) override
    {
        auto buffer = magma::PlatformBuffer::Import(command_buffer_handle);
        EXPECT_EQ(buffer->id(), TestPlatformConnection::test_buffer_id);
        EXPECT_EQ(context_id, TestPlatformConnection::test_context_id);
        TestPlatformConnection::test_complete = true;
        return MAGMA_STATUS_OK;
    }

    bool MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                      uint64_t page_count, uint64_t flags) override
    {
        EXPECT_EQ(TestPlatformConnection::test_buffer_id, buffer_id);
        EXPECT_EQ(PAGE_SIZE * 1000lu, gpu_va);
        EXPECT_EQ(1u, page_offset);
        EXPECT_EQ(2u, page_count);
        EXPECT_EQ(5u, flags);
        return true;
    }

    bool UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override
    {
        EXPECT_EQ(TestPlatformConnection::test_buffer_id, buffer_id);
        EXPECT_EQ(PAGE_SIZE * 1000lu, gpu_va);
        return true;
    }

    bool CommitBuffer(uint64_t buffer_id, uint64_t page_offset, uint64_t page_count) override
    {
        EXPECT_EQ(TestPlatformConnection::test_buffer_id, buffer_id);
        EXPECT_EQ(1000lu, page_offset);
        EXPECT_EQ(2000lu, page_count);
        return true;
    }

    void SetNotificationCallback(msd_connection_notification_callback_t callback,
                                 void* token) override
    {
        if (!token) {
            TestPlatformConnection::test_complete = true;
        } else {
            uint32_t data = 5;
            msd_notification_t n = {.type = MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND};
            *reinterpret_cast<uint32_t*>(n.u.channel_send.data) = data;
            n.u.channel_send.size = sizeof(data);
            callback(token, &n);
        }
    }

    magma::Status ExecuteImmediateCommands(uint32_t context_id, uint64_t commands_size,
                                           void* commands, uint64_t semaphore_count,
                                           uint64_t* semaphores) override
    {
        EXPECT_GE(2048u, commands_size);
        uint8_t received_bytes[2048] = {};
        EXPECT_EQ(0, memcmp(received_bytes, commands, commands_size));
        uint64_t command_count = commands_size / kImmediateCommandSize;
        EXPECT_EQ(3u * command_count, semaphore_count);
        for (uint32_t i = 0; i < command_count; i++) {
            EXPECT_EQ(0u, semaphores[0]);
            EXPECT_EQ(1u, semaphores[1]);
            EXPECT_EQ(2u, semaphores[2]);
            semaphores += 3;
        }

        immediate_commands_bytes_executed_ += commands_size;
        TestPlatformConnection::test_complete = immediate_commands_bytes_executed_ == 4096;

        return MAGMA_STATUS_OK;
    }

    uint64_t immediate_commands_bytes_executed_ = 0;
};

std::unique_ptr<TestPlatformConnection> TestPlatformConnection::Create()
{
    test_buffer_id = 0xcafecafecafecafe;
    test_semaphore_id = ~0u;
    test_context_id = 0xdeadbeef;
    test_error = 0x12345678;
    test_complete = false;
    auto delegate = std::make_unique<TestDelegate>();

    auto connection = magma::PlatformConnection::Create(std::move(delegate));
    if (!connection)
        return DRETP(nullptr, "failed to create PlatformConnection");
    auto ipc_connection = magma::PlatformIpcConnection::Create(
        connection->GetHandle(), connection->GetNotificationChannel());
    if (!ipc_connection)
        return DRETP(nullptr, "failed to create PlatformIpcConnection");

    auto ipc_thread = std::thread(IpcThreadFunc, std::move(connection));

    return std::make_unique<TestPlatformConnection>(std::move(ipc_connection),
                                                    std::move(ipc_thread));
}

TEST(PlatformConnection, GetError)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestGetError();
}

TEST(PlatformConnection, ImportBuffer)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestImportBuffer();
}

TEST(PlatformConnection, ReleaseBuffer)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestReleaseBuffer();
}

TEST(PlatformConnection, ImportObject)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestImportObject();
}

TEST(PlatformConnection, ReleaseObject)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestReleaseObject();
}

TEST(PlatformConnection, CreateContext)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestCreateContext();
}

TEST(PlatformConnection, DestroyContext)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestDestroyContext();
}

TEST(PlatformConnection, ExecuteCommandBuffer)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestExecuteCommandBuffer();
}

TEST(PlatformConnection, MapUnmapBuffer)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestMapUnmapBuffer();
}

TEST(PlatformConnection, NotificationChannel)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestNotificationChannel();
}

TEST(PlatformConnection, ExecuteImmediateCommands)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestExecuteImmediateCommands();
}
