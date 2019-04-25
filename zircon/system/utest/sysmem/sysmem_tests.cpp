// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fbl/algorithm.h>
#include <fcntl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>

#include <limits>

// We assume one sysmem since boot, for now.
const char* kSysmemDevicePath = "/dev/class/sysmem/000";

extern const fidl_type_t fuchsia_sysmem_BufferCollectionConstraintsTable;
using BufferCollectionConstraints = FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
                                               &fuchsia_sysmem_BufferCollectionConstraintsTable>;
extern const fidl_type_t fuchsia_sysmem_BufferCollectionInfo_2Table;
using BufferCollectionInfo =
    FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2, &fuchsia_sysmem_BufferCollectionInfo_2Table>;

namespace {

zx_status_t connect_to_sysmem_driver(zx::channel* allocator2_client_param) {
    zx_status_t status;

    zx::channel driver_client;
    zx::channel driver_server;
    status = zx::channel::create(0, &driver_client, &driver_server);
    ASSERT_EQ(status, ZX_OK, "");

    status = fdio_service_connect(kSysmemDevicePath, driver_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel allocator2_client;
    zx::channel allocator2_server;
    status = zx::channel::create(0, &allocator2_client, &allocator2_server);
    ASSERT_EQ(status, ZX_OK, "");

    status =
        fuchsia_sysmem_DriverConnectorConnect(driver_client.get(), allocator2_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    *allocator2_client_param = std::move(allocator2_client);
    return ZX_OK;
}

zx_status_t connect_to_sysmem_service(zx::channel* allocator2_client_param) {
    zx_status_t status;

    zx::channel allocator2_client;
    zx::channel allocator2_server;
    status = zx::channel::create(0, &allocator2_client, &allocator2_server);
    ASSERT_EQ(status, ZX_OK, "");

    status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator2_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    *allocator2_client_param = std::move(allocator2_client);
    return ZX_OK;
}

zx_koid_t get_koid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status =
        zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK, "");
    return info.koid;
}

zx_status_t verify_connectivity(zx::channel& allocator2_client) {
    zx_status_t status;

    zx::channel collection_client;
    zx::channel collection_server;
    status = zx::channel::create(0, &collection_client, &collection_server);
    ASSERT_EQ(status, ZX_OK, "");

    status = fuchsia_sysmem_AllocatorAllocateNonSharedCollection(allocator2_client.get(),
                                                                  collection_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    status = fuchsia_sysmem_BufferCollectionSync(collection_client.get());
    ASSERT_EQ(status, ZX_OK, "");

    return ZX_OK;
}

} // namespace

extern "C" bool test_sysmem_driver_connection(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::channel allocator2_client;
    status = connect_to_sysmem_driver(&allocator2_client);
    ASSERT_EQ(status, ZX_OK, "");

    status = verify_connectivity(allocator2_client);
    ASSERT_EQ(status, ZX_OK, "");

    END_TEST;
}

extern "C" bool test_sysmem_service_connection(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::channel allocator2_client;
    status = connect_to_sysmem_service(&allocator2_client);
    ASSERT_EQ(status, ZX_OK, "");

    status = verify_connectivity(allocator2_client);
    ASSERT_EQ(status, ZX_OK, "");

    END_TEST;
}

extern "C" bool test_sysmem_token_one_participant_no_image_constraints(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::channel allocator2_client;
    status = connect_to_sysmem_driver(&allocator2_client);
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel token_client;
    zx::channel token_server;
    status = zx::channel::create(0, &token_client, &token_server);
    ASSERT_EQ(status, ZX_OK, "");

    status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client.get(),
                                                               token_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client;
    zx::channel collection_server;
    status = zx::channel::create(0, &collection_client, &collection_server);
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator2_client.get(), token_client.release(), collection_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
    constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
    constraints->min_buffer_count_for_camping = 3;
    constraints->has_buffer_memory_constraints = true;
    constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        .min_size_bytes = 64 * 1024,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = false,
        .secure_required = false,
        .secure_permitted = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
    };
    ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                           constraints.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, buffer_collection_info.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem_CoherencyDomain_Cpu, "");
    ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

    for (uint32_t i = 0; i < 64; ++i) {
        if (i < 3) {
            ASSERT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
            uint64_t size_bytes = 0;
            status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
            ASSERT_EQ(status, ZX_OK, "");
            ASSERT_EQ(size_bytes, 64 * 1024, "");
        } else {
            ASSERT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
        }
    }

    END_TEST;
}

extern "C" bool test_sysmem_token_one_participant_with_image_constraints(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::channel allocator2_client;
    status = connect_to_sysmem_driver(&allocator2_client);
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel token_client;
    zx::channel token_server;
    status = zx::channel::create(0, &token_client, &token_server);
    ASSERT_EQ(status, ZX_OK, "");

    status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client.get(),
                                                               token_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client;
    zx::channel collection_server;
    status = zx::channel::create(0, &collection_client, &collection_server);
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator2_client.get(), token_client.release(), collection_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
    constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
    constraints->min_buffer_count_for_camping = 3;
    constraints->has_buffer_memory_constraints = true;
    constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        // This min_size_bytes is intentionally too small to hold the min_coded_width and
        // min_coded_height in NV12
        // format.
        .min_size_bytes = 64 * 1024,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = false,
        .secure_required = false,
        .secure_permitted = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
    };
    constraints->image_format_constraints_count = 1;
    fuchsia_sysmem_ImageFormatConstraints& image_constraints =
        constraints->image_format_constraints[0];
    image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] = fuchsia_sysmem_ColorSpace{
        .type = fuchsia_sysmem_ColorSpaceType_REC709,
    };
    // The min dimensions intentionally imply a min size that's larger than
    // buffer_memory_constraints.min_size_bytes.
    image_constraints.min_coded_width = 256;
    image_constraints.max_coded_width = std::numeric_limits<uint32_t>::max();
    image_constraints.min_coded_height = 256;
    image_constraints.max_coded_height = std::numeric_limits<uint32_t>::max();
    image_constraints.min_bytes_per_row = 256;
    image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
    image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
    image_constraints.layers = 1;
    image_constraints.coded_width_divisor = 2;
    image_constraints.coded_height_divisor = 2;
    image_constraints.bytes_per_row_divisor = 2;
    image_constraints.start_offset_divisor = 2;
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;

    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                           constraints.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, buffer_collection_info.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
    // The size should be sufficient for the whole NV12 frame, not just min_size_bytes.
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024 * 3 / 2, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem_CoherencyDomain_Cpu, "");
    // We specified image_format_constraints so the result must also have
    // image_format_constraints.
    ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, true, "");

    for (uint32_t i = 0; i < 64; ++i) {
        if (i < 3) {
            ASSERT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
            uint64_t size_bytes = 0;
            status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
            ASSERT_EQ(status, ZX_OK, "");
            // The portion of the VMO the client can use is large enough to hold the min image size,
            // despite the min buffer size being smaller.
            ASSERT_GE(buffer_collection_info->settings.buffer_settings.size_bytes,
                      64 * 1024 * 3 / 2, "");
            // The vmo has room for the nominal size of the portion of the VMO the client can use.
            ASSERT_LE(buffer_collection_info->buffers[i].vmo_usable_start +
                          buffer_collection_info->settings.buffer_settings.size_bytes,
                      size_bytes, "");
        } else {
            ASSERT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
        }
    }

    END_TEST;
}

extern "C" bool test_sysmem_min_buffer_count(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::channel allocator_client;
    status = connect_to_sysmem_driver(&allocator_client);
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel token_client;
    zx::channel token_server;
    status = zx::channel::create(0, &token_client, &token_server);
    ASSERT_EQ(status, ZX_OK, "");

    status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                              token_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client;
    zx::channel collection_server;
    status = zx::channel::create(0, &collection_client, &collection_server);
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator_client.get(), token_client.release(), collection_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
    constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
    constraints->min_buffer_count_for_camping = 3;
    constraints->min_buffer_count = 5;
    constraints->has_buffer_memory_constraints = true;
    constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        .min_size_bytes = 64 * 1024,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = false,
        .secure_required = false,
        .secure_permitted = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
    };
    ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                           constraints.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, buffer_collection_info.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    ASSERT_EQ(buffer_collection_info->buffer_count, 5, "");

    END_TEST;
}

extern "C" bool test_sysmem_no_token(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::channel allocator2_client;
    status = connect_to_sysmem_driver(&allocator2_client);
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client;
    zx::channel collection_server;
    status = zx::channel::create(0, &collection_client, &collection_server);
    ASSERT_EQ(status, ZX_OK, "");

    status = fuchsia_sysmem_AllocatorAllocateNonSharedCollection(allocator2_client.get(),
                                                                  collection_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
    constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
    // Ask for display usage to encourage using the ram coherency domain.
    constraints->usage.display = fuchsia_sysmem_displayUsageLayer;
    constraints->min_buffer_count_for_camping = 3;
    constraints->has_buffer_memory_constraints = true;
    constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        .min_size_bytes = 64 * 1024,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = false,
        .secure_required = false,
        .secure_permitted = false,
        .ram_domain_supported = true,
        .cpu_domain_supported = true,
    };
    ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                           constraints.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, buffer_collection_info.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem_CoherencyDomain_Ram, "");
    ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

    for (uint32_t i = 0; i < 64; ++i) {
        if (i < 3) {
            ASSERT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
            uint64_t size_bytes = 0;
            status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
            ASSERT_EQ(status, ZX_OK, "");
            ASSERT_EQ(size_bytes, 64 * 1024, "");
        } else {
            ASSERT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
        }
    }

    END_TEST;
}

extern "C" bool test_sysmem_multiple_participants(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::channel allocator2_client_1;
    status = connect_to_sysmem_driver(&allocator2_client_1);
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel token_client_1;
    zx::channel token_server_1;
    status = zx::channel::create(0, &token_client_1, &token_server_1);
    ASSERT_EQ(status, ZX_OK, "");

    // Client 1 creates a token and new LogicalBufferCollection using
    // AllocateSharedCollection().
    status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client_1.get(),
                                                               token_server_1.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel token_client_2;
    zx::channel token_server_2;
    status = zx::channel::create(0, &token_client_2, &token_server_2);
    ASSERT_EQ(status, ZX_OK, "");

    // Client 1 duplicates its token and gives the duplicate to client 2 (this
    // test is single proc, so both clients are coming from this client
    // process - normally the two clients would be in separate processes with
    // token_client_2 transferred to another participant).
    status = fuchsia_sysmem_BufferCollectionTokenDuplicate(
        token_client_1.get(), std::numeric_limits<uint32_t>::max(), token_server_2.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel token_client_3;
    zx::channel token_server_3;
    status = zx::channel::create(0, &token_client_3, &token_server_3);
    ASSERT_EQ(status, ZX_OK, "");

    // Client 3 is used to test a participant that doesn't set any constraints
    // and only wants a notification that the allocation is done.
    status = fuchsia_sysmem_BufferCollectionTokenDuplicate(
        token_client_1.get(), std::numeric_limits<uint32_t>::max(), token_server_3.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client_1;
    zx::channel collection_server_1;
    status = zx::channel::create(0, &collection_client_1, &collection_server_1);
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client_1.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator2_client_1.get(), token_client_1.release(), collection_server_1.release());
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints_1(BufferCollectionConstraints::Default);
    constraints_1->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
    constraints_1->min_buffer_count_for_camping = 3;
    constraints_1->has_buffer_memory_constraints = true;
    constraints_1->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        // This min_size_bytes is intentionally too small to hold the min_coded_width and
        // min_coded_height in NV12
        // format.
        .min_size_bytes = 64 * 1024,
        // Allow a max that's just large enough to accomodate the size implied
        // by the min frame size and PixelFormat.
        .max_size_bytes = (512 * 512) * 3 / 2,
        .physically_contiguous_required = false,
        .secure_required = false,
        .secure_permitted = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
    };
    constraints_1->image_format_constraints_count = 1;
    fuchsia_sysmem_ImageFormatConstraints& image_constraints_1 =
        constraints_1->image_format_constraints[0];
    image_constraints_1.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
    image_constraints_1.color_spaces_count = 1;
    image_constraints_1.color_space[0] = fuchsia_sysmem_ColorSpace{
        .type = fuchsia_sysmem_ColorSpaceType_REC709,
    };
    // The min dimensions intentionally imply a min size that's larger than
    // buffer_memory_constraints.min_size_bytes.
    image_constraints_1.min_coded_width = 256;
    image_constraints_1.max_coded_width = std::numeric_limits<uint32_t>::max();
    image_constraints_1.min_coded_height = 256;
    image_constraints_1.max_coded_height = std::numeric_limits<uint32_t>::max();
    image_constraints_1.min_bytes_per_row = 256;
    image_constraints_1.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
    image_constraints_1.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
    image_constraints_1.layers = 1;
    image_constraints_1.coded_width_divisor = 2;
    image_constraints_1.coded_height_divisor = 2;
    image_constraints_1.bytes_per_row_divisor = 2;
    image_constraints_1.start_offset_divisor = 2;
    image_constraints_1.display_width_divisor = 1;
    image_constraints_1.display_height_divisor = 1;

    // Start with constraints_2 a copy of constraints_1.  There are no handles
    // in the constraints struct so a struct copy instead of clone is fine here.
    BufferCollectionConstraints constraints_2(*constraints_1.get());
    // Modify constraints_2 to require double the width and height.
    constraints_2->image_format_constraints[0].min_coded_width = 512;
    constraints_2->image_format_constraints[0].min_coded_height = 512;

    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_1.get(), true,
                                                           constraints_1.release());
    ASSERT_EQ(status, ZX_OK, "");

    // Client 2 connects to sysmem separately.
    zx::channel allocator2_client_2;
    status = connect_to_sysmem_driver(&allocator2_client_2);
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client_2;
    zx::channel collection_server_2;
    status = zx::channel::create(0, &collection_client_2, &collection_server_2);
    ASSERT_EQ(status, ZX_OK, "");

    // Just because we can, perform this sync as late as possible, just before
    // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
    // the BindSharedCollection() might arrive at the server before the
    // Duplicate() that delivered the server end of token_client_2 to sysmem,
    // which would cause sysmem to not recognize the token.
    status = fuchsia_sysmem_BufferCollectionSync(collection_client_1.get());
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client_2.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator2_client_2.get(), token_client_2.release(), collection_server_2.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client_3;
    zx::channel collection_server_3;
    status = zx::channel::create(0, &collection_client_3, &collection_server_3);
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client_3.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator2_client_2.get(), token_client_3.release(), collection_server_3.release());
    ASSERT_EQ(status, ZX_OK, "");

    fuchsia_sysmem_BufferCollectionConstraints empty_constraints = {};

    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_3.get(), false,
                                                           &empty_constraints);
    ASSERT_EQ(status, ZX_OK, "");

    // Not all constraints have been input, so the buffers haven't been
    // allocated yet.
    zx_status_t check_status;
    status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_1.get(), &check_status);
    ASSERT_EQ(status, ZX_OK, "");
    EXPECT_EQ(check_status, ZX_ERR_UNAVAILABLE, "");
    status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_2.get(), &check_status);
    ASSERT_EQ(status, ZX_OK, "");
    EXPECT_EQ(check_status, ZX_ERR_UNAVAILABLE, "");

    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_2.get(), true,
                                                           constraints_2.release());
    ASSERT_EQ(status, ZX_OK, "");

    //
    // Only after both participants (both clients) have SetConstraints() will
    // the allocation be successful.
    //

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info_1(BufferCollectionInfo::Default);
    // This helps with a later exact equality check.
    memset(buffer_collection_info_1.get(), 0, sizeof(*buffer_collection_info_1.get()));
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client_1.get(), &allocation_status, buffer_collection_info_1.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_1.get(), &check_status);
    ASSERT_EQ(status, ZX_OK, "");
    EXPECT_EQ(check_status, ZX_OK, "");
    status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_2.get(), &check_status);
    ASSERT_EQ(status, ZX_OK, "");
    EXPECT_EQ(check_status, ZX_OK, "");

    BufferCollectionInfo buffer_collection_info_2(BufferCollectionInfo::Default);
    // This helps with a later exact equality check.
    memset(buffer_collection_info_2.get(), 0, sizeof(*buffer_collection_info_2.get()));
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client_2.get(), &allocation_status, buffer_collection_info_2.get());
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    BufferCollectionInfo buffer_collection_info_3(BufferCollectionInfo::Default);
    memset(buffer_collection_info_3.get(), 0, sizeof(*buffer_collection_info_3.get()));
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client_3.get(), &allocation_status, buffer_collection_info_3.get());
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    //
    // buffer_collection_info_1 and buffer_collection_info_2 should be exactly
    // equal except their non-zero handle values, which should be different.  We
    // verify the handle values then check that the structs are exactly the same
    // with handle values zeroed out.
    //

    // copy_1 and copy_2 intentionally don't manage their handle values.

    // struct copy
    fuchsia_sysmem_BufferCollectionInfo_2 copy_1 = *buffer_collection_info_1.get();
    // struct copy
    fuchsia_sysmem_BufferCollectionInfo_2 copy_2 = *buffer_collection_info_2.get();
    for (uint32_t i = 0; i < fbl::count_of(buffer_collection_info_1->buffers); ++i) {
        ASSERT_EQ(buffer_collection_info_1->buffers[i].vmo != ZX_HANDLE_INVALID,
                  buffer_collection_info_2->buffers[i].vmo != ZX_HANDLE_INVALID, "");
        if (buffer_collection_info_1->buffers[i].vmo != ZX_HANDLE_INVALID) {
            // The handle values must be different.
            ASSERT_NE(buffer_collection_info_1->buffers[i].vmo,
                      buffer_collection_info_2->buffers[i].vmo, "");
            // For now, the koid(s) are expected to be equal.  This is not a
            // fundamental check, in that sysmem could legitimately change in
            // future to vend separate child VMOs (of the same portion of a
            // non-copy-on-write parent VMO) to the two participants and that
            // would still be potentially valid overall.
            zx_koid_t koid_1 = get_koid(buffer_collection_info_1->buffers[i].vmo);
            zx_koid_t koid_2 = get_koid(buffer_collection_info_2->buffers[i].vmo);
            ASSERT_EQ(koid_1, koid_2, "");

            // Prepare the copies for memcmp().
            copy_1.buffers[i].vmo = ZX_HANDLE_INVALID;
            copy_2.buffers[i].vmo = ZX_HANDLE_INVALID;
        }

        // Buffer collection 3 never got a SetConstraints(), so we get no VMOs.
        ASSERT_EQ(ZX_HANDLE_INVALID, buffer_collection_info_3->buffers[i].vmo, "");
    }
    int32_t memcmp_result = memcmp(&copy_1, &copy_2, sizeof(copy_1));
    // Check that buffer_collection_info_1 and buffer_collection_info_2 are
    // consistent.
    ASSERT_EQ(memcmp_result, 0, "");

    memcmp_result = memcmp(&copy_1, buffer_collection_info_3.get(), sizeof(copy_1));
    // Check that buffer_collection_info_1 and buffer_collection_info_3 are
    // consistent, except for the vmos.
    ASSERT_EQ(memcmp_result, 0, "");

    //
    // Verify that buffer_collection_info_1 paid attention to constraints_2, and
    // that buffer_collection_info_2 makes sense.
    //

    // Because each specified min_buffer_count_for_camping 3, and each
    // participant camping count adds together since they camp independently.
    ASSERT_EQ(buffer_collection_info_1->buffer_count, 6, "");
    // The size should be sufficient for the whole NV12 frame, not just
    // min_size_bytes.  In other words, the portion of the VMO the client can
    // use is large enough to hold the min image size, despite the min buffer
    // size being smaller.
    ASSERT_GE(buffer_collection_info_1->settings.buffer_settings.size_bytes, (512 * 512) * 3 / 2,
              "");
    ASSERT_EQ(buffer_collection_info_1->settings.buffer_settings.is_physically_contiguous, false,
              "");
    ASSERT_EQ(buffer_collection_info_1->settings.buffer_settings.is_secure, false, "");
    // We specified image_format_constraints so the result must also have
    // image_format_constraints.
    ASSERT_EQ(buffer_collection_info_1->settings.has_image_format_constraints, true, "");

    for (uint32_t i = 0; i < 64; ++i) {
        if (i < 6) {
            ASSERT_NE(buffer_collection_info_1->buffers[i].vmo, ZX_HANDLE_INVALID, "");
            ASSERT_NE(buffer_collection_info_2->buffers[i].vmo, ZX_HANDLE_INVALID, "");

            uint64_t size_bytes_1 = 0;
            status = zx_vmo_get_size(buffer_collection_info_1->buffers[i].vmo, &size_bytes_1);
            ASSERT_EQ(status, ZX_OK, "");

            uint64_t size_bytes_2 = 0;
            status = zx_vmo_get_size(buffer_collection_info_2->buffers[i].vmo, &size_bytes_2);
            ASSERT_EQ(status, ZX_OK, "");

            // The vmo has room for the nominal size of the portion of the VMO
            // the client can use.  These checks should pass even if sysmem were
            // to vend different child VMOs to the two participants.
            ASSERT_LE(buffer_collection_info_1->buffers[i].vmo_usable_start +
                          buffer_collection_info_1->settings.buffer_settings.size_bytes,
                      size_bytes_1, "");
            ASSERT_LE(buffer_collection_info_2->buffers[i].vmo_usable_start +
                          buffer_collection_info_2->settings.buffer_settings.size_bytes,
                      size_bytes_2, "");
        } else {
            ASSERT_EQ(buffer_collection_info_1->buffers[i].vmo, ZX_HANDLE_INVALID, "");
            ASSERT_EQ(buffer_collection_info_2->buffers[i].vmo, ZX_HANDLE_INVALID, "");
        }
    }

    // Close to ensure grabbing null constraints from a closed collection
    // doesn't crash
    zx_status_t close_status = fuchsia_sysmem_BufferCollectionClose(collection_client_3.get());
    EXPECT_EQ(close_status, ZX_OK, "");

    END_TEST;
}

extern "C" bool test_sysmem_constraints_retained_beyond_clean_close() {
    BEGIN_TEST;

    zx_status_t status;
    zx::channel allocator2_client_1;
    status = connect_to_sysmem_driver(&allocator2_client_1);
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel token_client_1;
    zx::channel token_server_1;
    status = zx::channel::create(0, &token_client_1, &token_server_1);
    ASSERT_EQ(status, ZX_OK, "");

    // Client 1 creates a token and new LogicalBufferCollection using
    // AllocateSharedCollection().
    status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client_1.get(),
                                                               token_server_1.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel token_client_2;
    zx::channel token_server_2;
    status = zx::channel::create(0, &token_client_2, &token_server_2);
    ASSERT_EQ(status, ZX_OK, "");

    // Client 1 duplicates its token and gives the duplicate to client 2 (this
    // test is single proc, so both clients are coming from this client
    // process - normally the two clients would be in separate processes with
    // token_client_2 transferred to another participant).
    status = fuchsia_sysmem_BufferCollectionTokenDuplicate(
        token_client_1.get(), std::numeric_limits<uint32_t>::max(), token_server_2.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client_1;
    zx::channel collection_server_1;
    status = zx::channel::create(0, &collection_client_1, &collection_server_1);
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client_1.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator2_client_1.get(), token_client_1.release(), collection_server_1.release());
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints_1(BufferCollectionConstraints::Default);
    constraints_1->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
    constraints_1->min_buffer_count_for_camping = 2;
    constraints_1->has_buffer_memory_constraints = true;
    constraints_1->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        .min_size_bytes = 64 * 1024,
        .max_size_bytes = 64 * 1024,
        .physically_contiguous_required = false,
        .secure_required = false,
        .secure_permitted = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
    };

    // constraints_2 is just a copy of constraints_1 - since both participants
    // specify min_buffer_count_for_camping 2, the total number of allocated
    // buffers will be 4.  There are no handles in the constraints struct so a
    // struct copy instead of clone is fine here.
    BufferCollectionConstraints constraints_2(*constraints_1.get());
    ASSERT_EQ(constraints_2->min_buffer_count_for_camping, 2, "");

    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_1.get(), true,
                                                           constraints_1.release());
    ASSERT_EQ(status, ZX_OK, "");

    // Client 2 connects to sysmem separately.
    zx::channel allocator2_client_2;
    status = connect_to_sysmem_driver(&allocator2_client_2);
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client_2;
    zx::channel collection_server_2;
    status = zx::channel::create(0, &collection_client_2, &collection_server_2);
    ASSERT_EQ(status, ZX_OK, "");

    // Just because we can, perform this sync as late as possible, just before
    // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
    // the BindSharedCollection() might arrive at the server before the
    // Duplicate() that delivered the server end of token_client_2 to sysmem,
    // which would cause sysmem to not recognize the token.
    status = fuchsia_sysmem_BufferCollectionSync(collection_client_1.get());
    ASSERT_EQ(status, ZX_OK, "");

    // client 1 will now do a clean Close(), but client 1's constraints will be
    // retained by the LogicalBufferCollection.
    status = fuchsia_sysmem_BufferCollectionClose(collection_client_1.get());
    ASSERT_EQ(status, ZX_OK, "");
    // close client 1's channel.
    collection_client_1.reset();

    // Wait briefly so that LogicalBufferCollection will have seen the channel
    // closure of client 1 before client 2 sets constraints.  If we wanted to
    // eliminate this sleep we could add a call to query how many
    // BufferCollection views still exist per LogicalBufferCollection, but that
    // call wouldn't be meant to be used by normal clients, so it seems best to
    // avoid adding such a call.
    status = zx_nanosleep(zx_deadline_after(ZX_MSEC(250)));
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client_2.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator2_client_2.get(), token_client_2.release(), collection_server_2.release());
    ASSERT_EQ(status, ZX_OK, "");

    // Not all constraints have been input (client 2 hasn't SetConstraints()
    // yet), so the buffers haven't been allocated yet.
    zx_status_t check_status;
    status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_2.get(), &check_status);
    ASSERT_EQ(status, ZX_OK, "");
    EXPECT_EQ(check_status, ZX_ERR_UNAVAILABLE, "");

    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_2.get(), true,
                                                           constraints_2.release());
    ASSERT_EQ(status, ZX_OK, "");

    //
    // Now that client 2 has SetConstraints(), the allocation will proceed, with
    // client 1's constraints included despite client 1 having done a clean
    // Close().
    //

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info_2(BufferCollectionInfo::Default);
    // This helps with a later exact equality check.
    memset(buffer_collection_info_2.get(), 0, sizeof(*buffer_collection_info_2.get()));
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client_2.get(), &allocation_status, buffer_collection_info_2.get());
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    // The fact that this is 4 instead of 2 proves that client 1's constraints
    // were taken into account.
    ASSERT_EQ(buffer_collection_info_2->buffer_count, 4, "");

    END_TEST;
}

// clang-format off
BEGIN_TEST_CASE(sysmem_tests)
    RUN_TEST(test_sysmem_driver_connection)
    RUN_TEST(test_sysmem_service_connection)
    RUN_TEST(test_sysmem_token_one_participant_no_image_constraints)
    RUN_TEST(test_sysmem_token_one_participant_with_image_constraints)
    RUN_TEST(test_sysmem_min_buffer_count)
    RUN_TEST(test_sysmem_no_token)
    RUN_TEST(test_sysmem_multiple_participants)
    RUN_TEST(test_sysmem_constraints_retained_beyond_clean_close)
END_TEST_CASE(sysmem_tests)
// clang-format on
