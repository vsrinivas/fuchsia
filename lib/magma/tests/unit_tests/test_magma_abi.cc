// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include "magma.h"
#include "platform_buffer.h"
#include "gtest/gtest.h"
#include <thread>

class TestBase {
public:
    TestBase() { fd_ = open("/dev/class/display/000", O_RDONLY); }

    int fd() { return fd_; }

    ~TestBase() { close(fd_); }

    void GetDeviceId()
    {
        uint64_t device_id = 0;
        EXPECT_EQ(MAGMA_STATUS_OK, magma_query(fd_, MAGMA_QUERY_DEVICE_ID, &device_id));
        EXPECT_NE(0u, device_id);
    }

private:
    int fd_;
};

class TestConnection : public TestBase {
public:
    TestConnection() { connection_ = magma_open(fd(), MAGMA_CAPABILITY_RENDERING); }

    ~TestConnection()
    {
        if (connection_)
            magma_close(connection_);
    }

    void Connection() { ASSERT_NE(connection_, nullptr); }

    void Context()
    {
        ASSERT_NE(connection_, nullptr);

        uint32_t context_id[2];

        magma_create_context(connection_, &context_id[0]);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_create_context(connection_, &context_id[1]);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_destroy_context(connection_, context_id[0]);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_destroy_context(connection_, context_id[1]);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_destroy_context(connection_, context_id[1]);
        EXPECT_NE(magma_get_error(connection_), 0);
    }

    void Buffer()
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        uint64_t actual_size;
        uint64_t id;

        EXPECT_EQ(magma_alloc(connection_, size, &actual_size, &id), 0);
        EXPECT_GE(size, actual_size);
        EXPECT_NE(id, 0u);

        magma_free(connection_, id);
    }

    void WaitRendering()
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        uint64_t id;

        EXPECT_EQ(magma_alloc(connection_, size, &size, &id), 0);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_wait_rendering(connection_, id);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_free(connection_, id);
        EXPECT_EQ(magma_get_error(connection_), 0);
    }

    void BufferExport(uint32_t* handle_out, uint64_t* id_out)
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        magma_buffer_t buffer;

        EXPECT_EQ(magma_alloc(connection_, size, &size, &buffer), 0);

        *id_out = magma_get_buffer_id(buffer);

        EXPECT_EQ(magma_export(connection_, buffer, handle_out), 0);
    }

    void BufferImport(uint32_t handle, uint64_t id)
    {
        ASSERT_NE(connection_, nullptr);

        magma_buffer_t buffer;
        EXPECT_EQ(magma_import(connection_, handle, &buffer), 0);
        EXPECT_EQ(magma_get_buffer_id(buffer), id);

        // import twice for funsies
        EXPECT_EQ(magma_import(connection_, handle, &buffer), 0);
    }

    static void BufferImportExport(TestConnection* test1, TestConnection* test2)
    {
        uint32_t handle;
        uint64_t id;
        test1->BufferExport(&handle, &id);
        test2->BufferImport(handle, id);
    }

    void Semaphore()
    {
        ASSERT_NE(connection_, nullptr);

        magma_semaphore_t semaphore;
        EXPECT_EQ(MAGMA_STATUS_OK, magma_create_semaphore(connection_, &semaphore));

        EXPECT_NE(0u, magma_get_semaphore_id(semaphore));

        std::thread thread([semaphore] {
            EXPECT_EQ(MAGMA_STATUS_OK, magma_wait_semaphore(semaphore, 1000));
            EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, magma_wait_semaphore(semaphore, 100));
        });

        magma_signal_semaphore(semaphore);
        thread.join();

        magma_signal_semaphore(semaphore);
        magma_reset_semaphore(semaphore);

        thread = std::thread([semaphore] {
            EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, magma_wait_semaphore(semaphore, 100));
        });
        thread.join();

        magma_destroy_semaphore(connection_, semaphore);
    }

private:
    magma_connection_t* connection_;
};

TEST(MagmaAbi, DeviceId)
{
    TestBase test;
    test.GetDeviceId();
}

TEST(MagmaAbi, Buffer)
{
    TestConnection test;
    test.Buffer();
}

TEST(MagmaAbi, Connection)
{
    TestConnection test;
    test.Connection();
}

TEST(MagmaAbi, Context)
{
    TestConnection test;
    test.Context();
}

TEST(MagmaAbi, WaitRendering)
{
    TestConnection test;
    test.WaitRendering();
}

TEST(MagmaAbi, BufferImportExport)
{
    TestConnection test1;
    TestConnection test2;
    TestConnection::BufferImportExport(&test1, &test2);
}

TEST(MagmaAbi, Semaphore)
{
    TestConnection test;
    test.Semaphore();
}
