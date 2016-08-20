// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "magma_connection.h"
#include "gtest/gtest.h"

#define BATCH_SIZE (8192 * sizeof(uint32_t))
#define BUF_SIZE 4096
#define BUF_ALIGN 0

TEST(MagmaBuffer, RelocationManagement)
{
    int32_t ret;
    auto connection = magma_bufmgr_gem_init(0xdeadbeef, BATCH_SIZE);
    ASSERT_NE(connection, nullptr);
    auto buf0 = magma_bo_alloc(connection, "test buf 0", BUF_SIZE, BUF_ALIGN);
    ASSERT_NE(buf0, nullptr);
    auto buf1 = magma_bo_alloc(connection, "test buf 1", BUF_SIZE, BUF_ALIGN);
    ASSERT_NE(buf1, nullptr);

    EXPECT_EQ(magma_gem_bo_get_reloc_count(buf0), 0);
    EXPECT_EQ(magma_gem_bo_get_reloc_count(buf1), 0);

    int32_t num_relocs = 10;

    for (int32_t i = 0; i < num_relocs; i++) {
        ret = magma_bo_emit_reloc(buf0, 0, buf1, 0, 0, 0);
        ASSERT_EQ(ret, 0);
        EXPECT_EQ(magma_gem_bo_get_reloc_count(buf0), i + 1);
        EXPECT_EQ(magma_gem_bo_get_reloc_count(buf1), 0);
    }

    magma_gem_bo_clear_relocs(buf0, num_relocs / 2);
    EXPECT_EQ(magma_gem_bo_get_reloc_count(buf0), num_relocs - (num_relocs / 2));

    magma_gem_bo_clear_relocs(buf0, 0);
    EXPECT_EQ(magma_gem_bo_get_reloc_count(buf0), 0);

    magma_bufmgr_destroy(connection);
}

class TestMagmaBuffer {
public:
    static void TestPrepareForExecution(MagmaConnection* connection)
    {
        auto buf0 =
            MagmaBuffer::cast(magma_bo_alloc(connection, "test buf 0", BUF_SIZE, BUF_ALIGN));
        auto buf1 =
            MagmaBuffer::cast(magma_bo_alloc(connection, "test buf 1", BUF_SIZE, BUF_ALIGN));
        ASSERT_EQ(magma_bo_emit_reloc(buf0, 0, buf1, 0, 0, 0), 0);

        auto cmd_buf = buf0->PrepareForExecution();
        auto abi_cmd_buf = cmd_buf->abi_cmd_buf();

        EXPECT_EQ(abi_cmd_buf->batch_buffer_handle, buf0->handle);
        EXPECT_EQ(abi_cmd_buf->num_resources, 2u);
        EXPECT_TRUE((abi_cmd_buf->resources[0].buffer_handle == buf0->handle) ||
                    (abi_cmd_buf->resources[0].buffer_handle == buf1->handle));
        EXPECT_TRUE((abi_cmd_buf->resources[1].buffer_handle == buf0->handle) ||
                    (abi_cmd_buf->resources[1].buffer_handle == buf1->handle));
    }

    static void TestGetAbiExecResource(MagmaConnection* connection)
    {
        auto buf0 =
            MagmaBuffer::cast(magma_bo_alloc(connection, "test buf 0", BUF_SIZE, BUF_ALIGN));
        auto buf1 =
            MagmaBuffer::cast(magma_bo_alloc(connection, "test buf 1", BUF_SIZE, BUF_ALIGN));

        uint32_t offset = 0xdeadbeef;
        uint32_t target_offset = 0xcafed00d;
        uint32_t read_domains = 0x1;
        uint32_t write_domain = 0x2;

        ASSERT_EQ(
            magma_bo_emit_reloc(buf0, offset, buf1, target_offset, read_domains, write_domain), 0);

        magma_system_exec_resource resource;
        buf0->GetAbiExecResource(&resource,
                                 new magma_system_relocation_entry[buf0->RelocationCount()]);
        EXPECT_EQ(resource.buffer_handle, buf0->handle);
        EXPECT_EQ(resource.num_relocations, 1u);
        EXPECT_EQ(resource.relocations[0].offset, offset);
        EXPECT_EQ(resource.relocations[0].target_buffer_handle, buf1->handle);
        EXPECT_EQ(resource.relocations[0].target_offset, target_offset);
        EXPECT_EQ(resource.relocations[0].read_domains_bitfield, read_domains);
        EXPECT_EQ(resource.relocations[0].write_domains_bitfield, write_domain);

        delete resource.relocations;
    }

    static void TestGenerateExecResourceSet(MagmaConnection* connection)
    {
        auto buf0 =
            MagmaBuffer::cast(magma_bo_alloc(connection, "test buf 0", BUF_SIZE, BUF_ALIGN));
        auto buf1 =
            MagmaBuffer::cast(magma_bo_alloc(connection, "test buf 1", BUF_SIZE, BUF_ALIGN));
        auto buf2 =
            MagmaBuffer::cast(magma_bo_alloc(connection, "test buf 2", BUF_SIZE, BUF_ALIGN));

        // Tree relocation graph
        ASSERT_EQ(magma_bo_emit_reloc(buf0, 0, buf1, 0, 0, 0), 0);
        ASSERT_EQ(magma_bo_emit_reloc(buf1, 0, buf2, 0, 0, 0), 0);

        std::set<MagmaBuffer*> resources;
        MagmaBuffer::cast(buf0)->GenerateExecResourceSet(resources);
        EXPECT_EQ(resources.size(), 3u);
        EXPECT_NE(resources.find(buf0), resources.end());
        EXPECT_NE(resources.find(buf1), resources.end());
        EXPECT_NE(resources.find(buf2), resources.end());

        magma_gem_bo_clear_relocs(buf0, 0);
        magma_gem_bo_clear_relocs(buf1, 0);
        magma_gem_bo_clear_relocs(buf2, 0);
        resources.clear();

        // Diamond relocation graph
        ASSERT_EQ(magma_bo_emit_reloc(buf0, 0, buf1, 0, 0, 0), 0);
        ASSERT_EQ(magma_bo_emit_reloc(buf1, 0, buf2, 0, 0, 0), 0);
        ASSERT_EQ(magma_bo_emit_reloc(buf0, 0, buf2, 0, 0, 0), 0);

        MagmaBuffer::cast(buf0)->GenerateExecResourceSet(resources);
        EXPECT_EQ(resources.size(), 3u);
        EXPECT_NE(resources.find(buf0), resources.end());
        EXPECT_NE(resources.find(buf1), resources.end());
        EXPECT_NE(resources.find(buf2), resources.end());

        magma_gem_bo_clear_relocs(buf0, 0);
        magma_gem_bo_clear_relocs(buf1, 0);
        magma_gem_bo_clear_relocs(buf2, 0);
        resources.clear();

        // Cyclic relocation graph
        ASSERT_EQ(magma_bo_emit_reloc(buf0, 0, buf1, 0, 0, 0), 0);
        ASSERT_EQ(magma_bo_emit_reloc(buf1, 0, buf2, 0, 0, 0), 0);
        ASSERT_EQ(magma_bo_emit_reloc(buf2, 0, buf1, 0, 0, 0), 0);

        MagmaBuffer::cast(buf0)->GenerateExecResourceSet(resources);
        EXPECT_EQ(resources.size(), 3u);
        EXPECT_NE(resources.find(buf0), resources.end());
        EXPECT_NE(resources.find(buf1), resources.end());
        EXPECT_NE(resources.find(buf2), resources.end());

        magma_gem_bo_clear_relocs(buf0, 0);
        magma_gem_bo_clear_relocs(buf1, 0);
        magma_gem_bo_clear_relocs(buf2, 0);
        resources.clear();
    }
};

TEST(MagmaBuffer, PrepareForExecution)
{
    auto connection = MagmaConnection::cast(magma_bufmgr_gem_init(0xdeadbeef, BATCH_SIZE));

    TestMagmaBuffer::TestPrepareForExecution(connection);

    magma_bufmgr_destroy(connection);
}

TEST(MagmaBuffer, GetAbiExecResource)
{
    auto connection = MagmaConnection::cast(magma_bufmgr_gem_init(0xdeadbeef, BATCH_SIZE));

    TestMagmaBuffer::TestGetAbiExecResource(connection);

    magma_bufmgr_destroy(connection);
}

TEST(MagmaBuffer, GenerateExecResourceSet)
{
    auto connection = MagmaConnection::cast(magma_bufmgr_gem_init(0xdeadbeef, BATCH_SIZE));

    TestMagmaBuffer::TestGenerateExecResourceSet(connection);

    magma_bufmgr_destroy(connection);
}