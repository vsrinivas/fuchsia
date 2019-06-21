// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/goldfish/address/space/c/fidl.h>
#include <fuchsia/hardware/goldfish/control/c/fidl.h>
#include <fuchsia/hardware/goldfish/pipe/c/fidl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

static bool GoldfishPipeTest() {
    BEGIN_TEST;

    int fd = open("/dev/class/goldfish-pipe/000", O_RDWR);
    EXPECT_GE(fd, 0);

    zx::channel channel;
    EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()),
              ZX_OK);

    int32_t res;
    const size_t kSize = 3 * 4096;
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceSetBufferSize(channel.get(),
                                                                 kSize, &res),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);

    zx::vmo vmo;
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceGetBuffer(
                  channel.get(), &res, vmo.reset_and_get_address()),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);

    // Connect to pingpong service.
    constexpr char kPipeName[] = "pipe:pingpong";
    size_t bytes = strlen(kPipeName) + 1;
    EXPECT_EQ(vmo.write(kPipeName, 0, bytes), ZX_OK);
    uint64_t actual;
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceWrite(channel.get(), bytes,
                                                         0, &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, bytes);

    // Write 1 byte.
    const uint8_t kSentinel = 0xaa;
    EXPECT_EQ(vmo.write(&kSentinel, 0, 1), ZX_OK);
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceWrite(channel.get(), 1, 0,
                                                         &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, 1);

    // Read 1 byte result.
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceRead(channel.get(), 1, 0,
                                                        &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, 1);
    uint8_t result = 0;
    EXPECT_EQ(vmo.read(&result, 0, 1), ZX_OK);

    // pingpong service should have returned the data received.
    EXPECT_EQ(result, kSentinel);

    // Write 3 * 4096 bytes.
    uint8_t send_buffer[kSize];
    memset(send_buffer, kSentinel, kSize);
    EXPECT_EQ(vmo.write(send_buffer, 0, kSize), ZX_OK);
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceWrite(channel.get(), kSize,
                                                         0, &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, kSize);

    // Read 3 * 4096 bytes.
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceRead(channel.get(), kSize, 0,
                                                        &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, kSize);
    uint8_t recv_buffer[kSize];
    EXPECT_EQ(vmo.read(recv_buffer, 0, kSize), ZX_OK);

    // pingpong service should have returned the data received.
    EXPECT_EQ(memcmp(send_buffer, recv_buffer, kSize), 0);

    END_TEST;
}

BEGIN_TEST_CASE(GoldfishPipeTests)
RUN_TEST(GoldfishPipeTest)
END_TEST_CASE(GoldfishPipeTests)

extern const fidl_type_t fuchsia_sysmem_BufferCollectionConstraintsTable;
using BufferCollectionConstraints =
    FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
               &fuchsia_sysmem_BufferCollectionConstraintsTable>;
extern const fidl_type_t fuchsia_sysmem_BufferCollectionInfo_2Table;
using BufferCollectionInfo =
    FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2,
               &fuchsia_sysmem_BufferCollectionInfo_2Table>;

static bool GoldfishControlTest() {
    BEGIN_TEST;

    int fd = open("/dev/class/goldfish-control/000", O_RDWR);
    EXPECT_GE(fd, 0);

    zx::channel channel;
    EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()),
              ZX_OK);

    zx::channel allocator_client;
    zx::channel allocator_server;
    EXPECT_EQ(zx::channel::create(0, &allocator_client, &allocator_server),
              ZX_OK);
    EXPECT_EQ(fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                   allocator_server.release()),
              ZX_OK);

    zx::channel token_client;
    zx::channel token_server;
    EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
    EXPECT_EQ(fuchsia_sysmem_AllocatorAllocateSharedCollection(
                  allocator_client.get(), token_server.release()),
              ZX_OK);

    zx::channel collection_client;
    zx::channel collection_server;
    EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server),
              ZX_OK);
    EXPECT_EQ(fuchsia_sysmem_AllocatorBindSharedCollection(
                  allocator_client.get(), token_client.release(),
                  collection_server.release()),
              ZX_OK);

    BufferCollectionConstraints constraints(
        BufferCollectionConstraints::Default);
    constraints->usage.vulkan = fuchsia_sysmem_vulkanUsageTransferDst;
    constraints->min_buffer_count_for_camping = 1;
    constraints->has_buffer_memory_constraints = true;
    constraints->buffer_memory_constraints =
        fuchsia_sysmem_BufferMemoryConstraints{
            .min_size_bytes = 4 * 1024,
            .max_size_bytes = 4 * 1024,
            .physically_contiguous_required = false,
            .secure_required = false,
            .ram_domain_supported = false,
            .cpu_domain_supported = false,
            .inaccessible_domain_supported = true,
            .heap_permitted_count = 1,
            .heap_permitted = {fuchsia_sysmem_HeapType_GOLDFISH_DEVICE_LOCAL}};
    EXPECT_EQ(fuchsia_sysmem_BufferCollectionSetConstraints(
                  collection_client.get(), true, constraints.release()),
              ZX_OK);

    zx_status_t status2 = ZX_OK;
    BufferCollectionInfo info(BufferCollectionInfo::Default);
    EXPECT_EQ(fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
                  collection_client.get(), &status2, info.get()),
              ZX_OK);
    EXPECT_EQ(status2, ZX_OK);
    EXPECT_EQ(info->buffer_count, 1);
    EXPECT_NE(info->buffers[0].vmo, ZX_HANDLE_INVALID);

    zx::vmo vmo(info->buffers[0].vmo);
    info->buffers[0].vmo = ZX_HANDLE_INVALID;
    EXPECT_TRUE(vmo.is_valid());

    EXPECT_EQ(fuchsia_sysmem_BufferCollectionClose(collection_client.get()),
              ZX_OK);

    zx::vmo vmo_copy;
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

    status2 = ZX_OK;
    EXPECT_EQ(fuchsia_hardware_goldfish_control_DeviceCreateColorBuffer(
                  channel.get(), vmo_copy.release(), 64, 64,
                  fuchsia_hardware_goldfish_control_FormatType_BGRA, &status2),
              ZX_OK);
    EXPECT_EQ(status2, ZX_OK);

    zx::vmo vmo_copy2;
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);

    status2 = ZX_OK;
    uint32_t id = 0;
    EXPECT_EQ(fuchsia_hardware_goldfish_control_DeviceGetColorBuffer(
                  channel.get(), vmo_copy2.release(), &status2, &id),
              ZX_OK);
    EXPECT_EQ(status2, ZX_OK);
    EXPECT_NE(id, 0);

    END_TEST;
}

BEGIN_TEST_CASE(GoldfishControlTests)
RUN_TEST(GoldfishControlTest)
END_TEST_CASE(GoldfishControlTests)

static bool GoldfishAddressSpaceTest() {
    BEGIN_TEST;

    int fd = open("/dev/class/goldfish-address-space/000", O_RDWR);
    EXPECT_GE(fd, 0);

    zx::channel channel;
    EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()),
              ZX_OK);

    constexpr uint64_t kHeapSize = 512ULL * 1048576ULL;

    zx_status_t res;
    uint64_t actual_size = 0;
    uint64_t paddr = 0;
    zx::vmo vmo;
    EXPECT_EQ(fuchsia_hardware_goldfish_address_space_DeviceAllocateBlock(
                  channel.get(), kHeapSize, &res, &paddr,
                  vmo.reset_and_get_address()),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_NE(paddr, 0);
    EXPECT_EQ(vmo.is_valid(), true);
    EXPECT_EQ(vmo.get_size(&actual_size), ZX_OK);
    EXPECT_GE(actual_size, kHeapSize);

    uint64_t paddr2 = 0;
    zx::vmo vmo2;
    EXPECT_EQ(fuchsia_hardware_goldfish_address_space_DeviceAllocateBlock(
                  channel.get(), kHeapSize, &res, &paddr2,
                  vmo2.reset_and_get_address()),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_NE(paddr2, 0);
    EXPECT_NE(paddr2, paddr);
    EXPECT_EQ(vmo2.is_valid(), true);
    EXPECT_EQ(vmo.get_size(&actual_size), ZX_OK);
    EXPECT_GE(actual_size, kHeapSize);

    EXPECT_EQ(fuchsia_hardware_goldfish_address_space_DeviceDeallocateBlock(
                  channel.get(), paddr, &res),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);

    EXPECT_EQ(fuchsia_hardware_goldfish_address_space_DeviceDeallocateBlock(
                  channel.get(), paddr2, &res),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);

    END_TEST;
}

BEGIN_TEST_CASE(GoldfishAddressSpaceTests)
RUN_TEST(GoldfishAddressSpaceTest)
END_TEST_CASE(GoldfishAddressSpaceTests)

int main(int argc, char** argv) {
    if (access("/dev/sys/platform/acpi/goldfish", F_OK) != -1) {
        return unittest_run_all_tests(argc, argv) ? 0 : -1;
    }
    return 0;
}
