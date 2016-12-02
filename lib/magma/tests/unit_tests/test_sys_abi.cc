// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include "magma_system.h"
#include "platform_buffer.h"
#include "gtest/gtest.h"

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
    TestConnection() { connection_ = magma_system_open(fd(), MAGMA_SYSTEM_CAPABILITY_RENDERING); }

    ~TestConnection()
    {
        if (connection_)
            magma_system_close(connection_);
    }

    void Connection() { ASSERT_NE(connection_, nullptr); }

    void Context()
    {
        ASSERT_NE(connection_, nullptr);

        uint32_t context_id[2];

        magma_system_create_context(connection_, &context_id[0]);
        EXPECT_EQ(magma_system_get_error(connection_), 0);

        magma_system_create_context(connection_, &context_id[1]);
        EXPECT_EQ(magma_system_get_error(connection_), 0);

        magma_system_destroy_context(connection_, context_id[0]);
        EXPECT_EQ(magma_system_get_error(connection_), 0);

        magma_system_destroy_context(connection_, context_id[1]);
        EXPECT_EQ(magma_system_get_error(connection_), 0);

        magma_system_destroy_context(connection_, context_id[1]);
        EXPECT_NE(magma_system_get_error(connection_), 0);
    }

    void Buffer()
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        uint64_t actual_size;
        uint64_t id;

        EXPECT_EQ(magma_system_alloc(connection_, size, &actual_size, &id), 0);
        EXPECT_GE(size, actual_size);
        EXPECT_NE(id, 0u);

        magma_system_free(connection_, id);
    }

    void WaitRendering()
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        uint64_t id;

        EXPECT_EQ(magma_system_alloc(connection_, size, &size, &id), 0);
        EXPECT_EQ(magma_system_get_error(connection_), 0);

        magma_system_wait_rendering(connection_, id);
        EXPECT_EQ(magma_system_get_error(connection_), 0);

        magma_system_free(connection_, id);
        EXPECT_EQ(magma_system_get_error(connection_), 0);
    }

    void Export(uint32_t* handle_out, uint64_t* id_out)
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        magma_buffer_t buffer;

        EXPECT_EQ(magma_system_alloc(connection_, size, &size, &buffer), 0);

        *id_out = magma_system_get_buffer_id(buffer);

        EXPECT_EQ(magma_system_export(connection_, buffer, handle_out), 0);
    }

    void Import(uint32_t handle, uint64_t id)
    {
        ASSERT_NE(connection_, nullptr);

        magma_buffer_t buffer;
        EXPECT_EQ(magma_system_import(connection_, handle, &buffer), 0);
        EXPECT_EQ(magma_system_get_buffer_id(buffer), id);

        // import twice for funsies
        EXPECT_EQ(magma_system_import(connection_, handle, &buffer), 0);
    }

    static void ImportExport(TestConnection* test1, TestConnection* test2)
    {
        uint32_t handle;
        uint64_t id;
        test1->Export(&handle, &id);
        test2->Import(handle, id);
    }

private:
    magma_system_connection* connection_;
};

TEST(MagmaSystemAbi, DeviceId)
{
    TestBase test;
    test.GetDeviceId();
}

TEST(MagmaSystemAbi, Buffer)
{
    TestConnection test;
    test.Buffer();
}

TEST(MagmaSystemAbi, Connection)
{
    TestConnection test;
    test.Connection();
}

TEST(MagmaSystemAbi, Context)
{
    TestConnection test;
    test.Context();
}

TEST(MagmaSystemAbi, WaitRendering)
{
    TestConnection test;
    test.WaitRendering();
}

TEST(MagmaSystemAbi, ImportExport)
{
    TestConnection test1;
    TestConnection test2;
    TestConnection::ImportExport(&test1, &test2);
}
