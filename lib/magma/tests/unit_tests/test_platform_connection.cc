// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_connection.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>

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
        auto buf = magma::PlatformBuffer::Create(1);
        test_buffer_id = buf->id();
        EXPECT_EQ(ipc_connection_->ImportBuffer(buf.get()), 0);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }
    void TestReleaseBuffer()
    {
        auto buf = magma::PlatformBuffer::Create(1);
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
        ipc_connection_->ExecuteCommandBuffer(test_buffer_id, test_context_id);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }
    void TestWaitRendering()
    {
        ipc_connection_->WaitRendering(test_buffer_id);
        EXPECT_EQ(ipc_connection_->GetError(), 0);
    }

    void TestPageFlip()
    {
        ipc_connection_->PageFlip(test_buffer_id, PageFlipCallback, &test_buffer_id);
        // TODO(MA-118) eliminate need for this sleep
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    static void PageFlipCallback(magma_status_t error, void* data)
    {
        EXPECT_EQ(error, test_error);
        EXPECT_EQ(*reinterpret_cast<uint64_t*>(data), test_buffer_id);
    }

    void TestGetError()
    {
        EXPECT_EQ(ipc_connection_->GetError(), 0);
        test_complete = true;
    }

    static uint64_t test_buffer_id;
    static uint32_t test_context_id;
    static uint64_t test_semaphore_id;
    static magma_status_t test_error;
    static bool test_complete;

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

    magma::Status ExecuteCommandBuffer(uint64_t command_buffer_id, uint32_t context_id) override
    {
        EXPECT_EQ(command_buffer_id, TestPlatformConnection::test_buffer_id);
        EXPECT_EQ(context_id, TestPlatformConnection::test_context_id);
        TestPlatformConnection::test_complete = true;
        return MAGMA_STATUS_OK;
    }
    magma::Status WaitRendering(uint64_t buffer_id) override
    {
        EXPECT_EQ(buffer_id, TestPlatformConnection::test_buffer_id);
        TestPlatformConnection::test_complete = true;
        return MAGMA_STATUS_OK;
    }

    void PageFlip(uint64_t buffer_id, magma_system_pageflip_callback_t callback,
                  void* data) override
    {
        EXPECT_EQ(buffer_id, TestPlatformConnection::test_buffer_id);
        TestPlatformConnection::test_complete = true;
        callback(TestPlatformConnection::test_error, data);
    }
};

std::unique_ptr<TestPlatformConnection> TestPlatformConnection::Create()
{
    test_buffer_id = 0xcafecafecafecafe;
    test_context_id = 0xdeadbeef;
    test_error = 0x12345678;
    test_complete = false;
    auto delegate = std::make_unique<TestDelegate>();

    auto connection = magma::PlatformConnection::Create(std::move(delegate));
    if (!connection)
        return DRETP(nullptr, "failed to create PlatformConnection");
    auto ipc_connection = magma::PlatformIpcConnection::Create(connection->GetHandle());
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

TEST(PlatformConnection, WaitRendering)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestWaitRendering();
}

TEST(PlatformConnection, PageFlip)
{
    auto Test = TestPlatformConnection::Create();
    ASSERT_NE(Test, nullptr);
    Test->TestPageFlip();
}
