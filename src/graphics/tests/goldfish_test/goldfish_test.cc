// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

TEST(GoldfishPipeTests, GoldfishPipeTest) {
  int fd = open("/dev/class/goldfish-pipe/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  zx::channel pipe_client;
  zx::channel pipe_server;
  EXPECT_EQ(zx::channel::create(0, &pipe_client, &pipe_server), ZX_OK);

  llcpp::fuchsia::hardware::goldfish::PipeDevice::SyncClient pipe_device(std::move(channel));
  EXPECT_TRUE(pipe_device.OpenPipe(std::move(pipe_server)).ok());

  llcpp::fuchsia::hardware::goldfish::Pipe::SyncClient pipe(std::move(pipe_client));
  const size_t kSize = 3 * 4096;
  {
    auto result = pipe.SetBufferSize(kSize);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
  }

  zx::vmo vmo;
  {
    auto result = pipe.GetBuffer();
    ASSERT_TRUE(result.ok());
    vmo = std::move(result.Unwrap()->vmo);
  }

  // Connect to pingpong service.
  constexpr char kPipeName[] = "pipe:pingpong";
  size_t bytes = strlen(kPipeName) + 1;
  EXPECT_EQ(vmo.write(kPipeName, 0, bytes), ZX_OK);

  {
    auto result = pipe.Write(bytes, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_EQ(result.Unwrap()->actual, bytes);
  }

  // Write 1 byte.
  const uint8_t kSentinel = 0xaa;
  EXPECT_EQ(vmo.write(&kSentinel, 0, 1), ZX_OK);
  {
    auto result = pipe.Write(1, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_EQ(result.Unwrap()->actual, 1);
  }

  // Read 1 byte result.
  {
    auto result = pipe.Read(1, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_EQ(result.Unwrap()->actual, 1);
  }

  uint8_t result = 0;
  EXPECT_EQ(vmo.read(&result, 0, 1), ZX_OK);
  // pingpong service should have returned the data received.
  EXPECT_EQ(result, kSentinel);

  // Write 3 * 4096 bytes.
  uint8_t send_buffer[kSize];
  memset(send_buffer, kSentinel, kSize);
  EXPECT_EQ(vmo.write(send_buffer, 0, kSize), ZX_OK);
  {
    auto result = pipe.Write(kSize, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_EQ(result.Unwrap()->actual, kSize);
  }

  // Read 3 * 4096 bytes.
  {
    auto result = pipe.Read(kSize, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_EQ(result.Unwrap()->actual, kSize);
  }
  uint8_t recv_buffer[kSize];
  EXPECT_EQ(vmo.read(recv_buffer, 0, kSize), ZX_OK);

  // pingpong service should have returned the data received.
  EXPECT_EQ(memcmp(send_buffer, recv_buffer, kSize), 0);

  // Write & Read 4096 bytes.
  const size_t kSmallSize = kSize / 3;
  const size_t kRecvOffset = kSmallSize;
  memset(send_buffer, kSentinel, kSmallSize);
  EXPECT_EQ(vmo.write(send_buffer, 0, kSmallSize), ZX_OK);

  {
    auto result = pipe.DoCall(kSmallSize, 0u, kSmallSize, kRecvOffset);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_EQ(result.Unwrap()->actual, 2 * kSmallSize);
  }

  EXPECT_EQ(vmo.read(recv_buffer, kRecvOffset, kSmallSize), ZX_OK);

  // pingpong service should have returned the data received.
  EXPECT_EQ(memcmp(send_buffer, recv_buffer, kSmallSize), 0);
}

TEST(GoldfishControlTests, GoldfishControlTest) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  zx::channel allocator_client;
  zx::channel allocator_server;
  EXPECT_EQ(zx::channel::create(0, &allocator_client, &allocator_server), ZX_OK);
  EXPECT_EQ(fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator_server.release()),
            ZX_OK);

  llcpp::fuchsia::sysmem::Allocator::SyncClient allocator(std::move(allocator_client));

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator.AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator.BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = llcpp::fuchsia::sysmem::VULKAN_IMAGE_USAGE_TRANSFER_DST;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = llcpp::fuchsia::sysmem::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {llcpp::fuchsia::sysmem::HeapType::GOLDFISH_DEVICE_LOCAL}};

  llcpp::fuchsia::sysmem::BufferCollection::SyncClient collection(std::move(collection_client));
  EXPECT_TRUE(collection.SetConstraints(true, std::move(constraints)).ok());

  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 info;
  {
    auto result = collection.WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->status, ZX_OK);

    info = std::move(result.Unwrap()->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection.Close().ok());

  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  llcpp::fuchsia::hardware::goldfish::ControlDevice::SyncClient control(std::move(channel));
  {
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(64))
            .set_height(std::make_unique<uint32_t>(64))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();

    auto result = control.CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
  }

  zx::vmo vmo_copy2;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);

  {
    auto result = control.GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_NE(result.Unwrap()->id, 0u);
    EXPECT_EQ(result.Unwrap()->type,
              llcpp::fuchsia::hardware::goldfish::BufferHandleType::COLOR_BUFFER);
  }

  zx::vmo vmo_copy3;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy3), ZX_OK);

  {
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(64))
            .set_height(std::make_unique<uint32_t>(64))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();

    auto result = control.CreateColorBuffer2(std::move(vmo_copy3), std::move(create_params));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_ALREADY_EXISTS);
  }
}

TEST(GoldfishControlTests, GoldfishControlTest_HostVisible) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  zx::channel allocator_client;
  zx::channel allocator_server;
  EXPECT_EQ(zx::channel::create(0, &allocator_client, &allocator_server), ZX_OK);
  EXPECT_EQ(fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator_server.release()),
            ZX_OK);

  llcpp::fuchsia::sysmem::Allocator::SyncClient allocator(std::move(allocator_client));

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator.AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator.BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  const size_t kMinSizeBytes = 4 * 1024;
  const size_t kMaxSizeBytes = 4 * 4096;
  llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = llcpp::fuchsia::sysmem::VULKAN_IMAGE_USAGE_TRANSFER_DST;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = llcpp::fuchsia::sysmem::BufferMemoryConstraints{
      .min_size_bytes = kMinSizeBytes,
      .max_size_bytes = kMaxSizeBytes,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 1,
      .heap_permitted = {llcpp::fuchsia::sysmem::HeapType::GOLDFISH_HOST_VISIBLE}};
  constraints.image_format_constraints_count = 1;
  constraints.image_format_constraints[0] = llcpp::fuchsia::sysmem::ImageFormatConstraints{
      .pixel_format =
          llcpp::fuchsia::sysmem::PixelFormat{
              .type = llcpp::fuchsia::sysmem::PixelFormatType::BGRA32,
              .has_format_modifier = false,
              .format_modifier = {},
          },
      .color_spaces_count = 1,
      .color_space =
          {
              llcpp::fuchsia::sysmem::ColorSpace{.type =
                                                     llcpp::fuchsia::sysmem::ColorSpaceType::SRGB},
          },
      .min_coded_width = 32,
      .min_coded_height = 32,
  };

  llcpp::fuchsia::sysmem::BufferCollection::SyncClient collection(std::move(collection_client));
  EXPECT_TRUE(collection.SetConstraints(true, std::move(constraints)).ok());

  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 info;
  {
    auto result = collection.WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->status, ZX_OK);

    info = std::move(result.Unwrap()->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
    EXPECT_EQ(info.settings.buffer_settings.coherency_domain,
              llcpp::fuchsia::sysmem::CoherencyDomain::CPU);
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  uint64_t vmo_size;
  EXPECT_EQ(vmo.get_size(&vmo_size), ZX_OK);
  EXPECT_GE(vmo_size, kMinSizeBytes);
  EXPECT_LE(vmo_size, kMaxSizeBytes);

  // Test if the vmo is mappable.
  zx_vaddr_t addr;
  EXPECT_EQ(zx::vmar::root_self()->map(/*vmar_offset*/ 0, vmo, /*vmo_offset*/ 0, vmo_size,
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr),
            ZX_OK);

  // Test if write and read works correctly.
  uint8_t* ptr = reinterpret_cast<uint8_t*>(addr);
  std::vector<uint8_t> copy_target(vmo_size, 0u);
  for (uint32_t trial = 0; trial < 10u; trial++) {
    memset(ptr, trial, vmo_size);
    memcpy(copy_target.data(), ptr, vmo_size);
    zx_cache_flush(ptr, vmo_size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    EXPECT_EQ(memcmp(copy_target.data(), ptr, vmo_size), 0);
  }

  EXPECT_EQ(zx::vmar::root_self()->unmap(addr, PAGE_SIZE), ZX_OK);

  EXPECT_TRUE(collection.Close().ok());
}

TEST(GoldfishControlTests, GoldfishControlTest_DataBuffer) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  zx::channel allocator_client;
  zx::channel allocator_server;
  EXPECT_EQ(zx::channel::create(0, &allocator_client, &allocator_server), ZX_OK);
  EXPECT_EQ(fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator_server.release()),
            ZX_OK);

  llcpp::fuchsia::sysmem::Allocator::SyncClient allocator(std::move(allocator_client));

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator.AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator.BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  constexpr size_t kBufferSizeBytes = 4 * 1024;
  llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = llcpp::fuchsia::sysmem::VULKAN_BUFFER_USAGE_TRANSFER_DST;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = llcpp::fuchsia::sysmem::BufferMemoryConstraints{
      .min_size_bytes = kBufferSizeBytes,
      .max_size_bytes = kBufferSizeBytes,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {llcpp::fuchsia::sysmem::HeapType::GOLDFISH_DEVICE_LOCAL}};

  llcpp::fuchsia::sysmem::BufferCollection::SyncClient collection(std::move(collection_client));
  EXPECT_TRUE(collection.SetConstraints(true, std::move(constraints)).ok());

  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 info;
  {
    auto result = collection.WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->status, ZX_OK);

    info = std::move(result.Unwrap()->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection.Close().ok());

  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  llcpp::fuchsia::hardware::goldfish::ControlDevice::SyncClient control(std::move(channel));
  {
    auto result = control.CreateBuffer(std::move(vmo_copy), kBufferSizeBytes);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
  }

  zx::vmo vmo_copy2;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);

  {
    auto result = control.GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_NE(result.Unwrap()->id, 0u);
    EXPECT_EQ(result.Unwrap()->type, llcpp::fuchsia::hardware::goldfish::BufferHandleType::BUFFER);
  }

  zx::vmo vmo_copy3;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy3), ZX_OK);

  {
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(64))
            .set_height(std::make_unique<uint32_t>(64))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();

    auto result = control.CreateColorBuffer2(std::move(vmo_copy3), std::move(create_params));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_ALREADY_EXISTS);
  }

  zx::vmo vmo_copy4;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy4), ZX_OK);

  {
    auto result = control.CreateBuffer(std::move(vmo_copy4), kBufferSizeBytes);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_ALREADY_EXISTS);
  }
}

// In this test case we call CreateColorBuffer() and GetBufferHandle()
// on VMOs not registered with goldfish sysmem heap.
//
// The IPC transmission should succeed but FIDL interface should
// return ZX_ERR_INVALID_ARGS.
TEST(GoldfishControlTests, GoldfishControlTest_InvalidVmo) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  zx::vmo non_sysmem_vmo;
  EXPECT_EQ(zx::vmo::create(1024u, 0u, &non_sysmem_vmo), ZX_OK);

  // Call CreateColorBuffer() using vmo not registered with goldfish
  // sysmem heap.
  zx::vmo vmo_copy;
  EXPECT_EQ(non_sysmem_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  llcpp::fuchsia::hardware::goldfish::ControlDevice::SyncClient control(std::move(channel));
  {
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(16))
            .set_height(std::make_unique<uint32_t>(16))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();

    auto result = control.CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
  }

  // Call GetBufferHandle() using vmo not registered with goldfish
  // sysmem heap.
  zx::vmo vmo_copy2;
  EXPECT_EQ(non_sysmem_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);

  {
    auto result = control.GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
  }
}

// In this test case we test arguments of CreateColorBuffer2() method.
// If a mandatory field is missing, it should return "ZX_ERR_INVALID_ARGS".
TEST(GoldfishControlTests, GoldfishControlTest_CreateColorBuffer2Args) {
  // Setup control device.
  int control_device_fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(control_device_fd, 0);

  zx::channel control_channel;
  EXPECT_EQ(fdio_get_service_handle(control_device_fd, control_channel.reset_and_get_address()),
            ZX_OK);

  // ----------------------------------------------------------------------//
  // Setup sysmem allocator and buffer collection.
  zx::channel allocator_client;
  zx::channel allocator_server;
  EXPECT_EQ(zx::channel::create(0, &allocator_client, &allocator_server), ZX_OK);
  EXPECT_EQ(fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator_server.release()),
            ZX_OK);

  llcpp::fuchsia::sysmem::Allocator::SyncClient allocator(std::move(allocator_client));

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator.AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator.BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  // ----------------------------------------------------------------------//
  // Use device local heap which only *registers* the koid of vmo to control
  // device.
  llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = llcpp::fuchsia::sysmem::VULKAN_IMAGE_USAGE_TRANSFER_DST;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = llcpp::fuchsia::sysmem::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {llcpp::fuchsia::sysmem::HeapType::GOLDFISH_DEVICE_LOCAL}};

  llcpp::fuchsia::sysmem::BufferCollection::SyncClient collection(std::move(collection_client));
  EXPECT_TRUE(collection.SetConstraints(true, std::move(constraints)).ok());

  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 info;
  {
    auto result = collection.WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->status, ZX_OK);

    info = std::move(result.Unwrap()->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection.Close().ok());

  // ----------------------------------------------------------------------//
  // Try creating color buffer.
  zx::vmo vmo_copy;
  llcpp::fuchsia::hardware::goldfish::ControlDevice::SyncClient control(std::move(control_channel));

  {
    // Verify that a CreateColorBuffer2() call without width will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            // Without width
            .set_height(std::make_unique<uint32_t>(64))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();
    auto result = control.CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result.Unwrap()->hw_address_page_offset, 0);
  }

  {
    // Verify that a CreateColorBuffer2() call without height will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(64))
            // Without height
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();
    auto result = control.CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result.Unwrap()->hw_address_page_offset, 0);
  }

  {
    // Verify that a CreateColorBuffer2() call without color format will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(64))
            .set_height(std::make_unique<uint32_t>(64))
            // Without format
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();
    auto result = control.CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result.Unwrap()->hw_address_page_offset, 0);
  }

  {
    // Verify that a CreateColorBuffer2() call without memory property will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(64))
            .set_height(std::make_unique<uint32_t>(64))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            // Without memory property
            .build();
    auto result = control.CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result.Unwrap()->hw_address_page_offset, 0);
  }
}

// In this test case we call GetBufferHandle() on a vmo
// registered to the control device but we haven't created
// the color buffer yet.
//
// The FIDL interface should return ZX_ERR_NOT_FOUND.
TEST(GoldfishControlTests, GoldfishControlTest_GetNotCreatedColorBuffer) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  zx::channel allocator_client;
  zx::channel allocator_server;
  EXPECT_EQ(zx::channel::create(0, &allocator_client, &allocator_server), ZX_OK);
  EXPECT_EQ(fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator_server.release()),
            ZX_OK);

  llcpp::fuchsia::sysmem::Allocator::SyncClient allocator(std::move(allocator_client));

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator.AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator.BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = llcpp::fuchsia::sysmem::VULKAN_IMAGE_USAGE_TRANSFER_DST;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = llcpp::fuchsia::sysmem::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {llcpp::fuchsia::sysmem::HeapType::GOLDFISH_DEVICE_LOCAL}};

  llcpp::fuchsia::sysmem::BufferCollection::SyncClient collection(std::move(collection_client));
  EXPECT_TRUE(collection.SetConstraints(true, std::move(constraints)).ok());

  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 info;
  {
    auto result = collection.WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->status, ZX_OK);

    info = std::move(result.Unwrap()->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection.Close().ok());

  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  llcpp::fuchsia::hardware::goldfish::ControlDevice::SyncClient control(std::move(channel));
  {
    auto result = control.GetBufferHandle(std::move(vmo_copy));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_NOT_FOUND);
  }
}

TEST(GoldfishAddressSpaceTests, GoldfishAddressSpaceTest) {
  int fd = open("/dev/class/goldfish-address-space/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel parent_channel;
  EXPECT_EQ(fdio_get_service_handle(fd, parent_channel.reset_and_get_address()), ZX_OK);

  zx::channel child_channel;
  zx::channel child_channel2;
  EXPECT_EQ(zx::channel::create(0, &child_channel, &child_channel2), ZX_OK);

  llcpp::fuchsia::hardware::goldfish::AddressSpaceDevice::SyncClient asd_parent(
      std::move(parent_channel));
  {
    auto result = asd_parent.OpenChildDriver(
        llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverType::DEFAULT,
        std::move(child_channel));
    ASSERT_TRUE(result.ok());
  }

  constexpr uint64_t kHeapSize = 16ULL * 1048576ULL;

  llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriver::SyncClient asd_child(
      std::move(child_channel2));
  uint64_t paddr = 0;
  zx::vmo vmo;
  {
    auto result = asd_child.AllocateBlock(kHeapSize);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);

    paddr = result.Unwrap()->paddr;
    EXPECT_NE(paddr, 0);

    vmo = std::move(result.Unwrap()->vmo);
    EXPECT_EQ(vmo.is_valid(), true);
    uint64_t actual_size = 0;
    EXPECT_EQ(vmo.get_size(&actual_size), ZX_OK);
    EXPECT_GE(actual_size, kHeapSize);
  }

  zx::vmo vmo2;
  uint64_t paddr2 = 0;
  {
    auto result = asd_child.AllocateBlock(kHeapSize);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);

    paddr2 = result.Unwrap()->paddr;
    EXPECT_NE(paddr2, 0);
    EXPECT_NE(paddr2, paddr);

    vmo2 = std::move(result.Unwrap()->vmo);
    EXPECT_EQ(vmo2.is_valid(), true);
    uint64_t actual_size = 0;
    EXPECT_EQ(vmo2.get_size(&actual_size), ZX_OK);
    EXPECT_GE(actual_size, kHeapSize);
  }

  {
    auto result = asd_child.DeallocateBlock(paddr);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
  }

  {
    auto result = asd_child.DeallocateBlock(paddr2);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
  }

  // No testing into this too much, as it's going to be child driver-specific.
  // Use fixed values for shared offset/size and ping metadata.
  const uint64_t shared_offset = 4096;
  const uint64_t shared_size = 4096;

  const uint64_t overlap_offsets[] = {
      4096,
      0,
      8191,
  };
  const uint64_t overlap_sizes[] = {
      2048,
      4097,
      4096,
  };

  const size_t overlaps_to_test = sizeof(overlap_offsets) / sizeof(overlap_offsets[0]);

  using llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverPingMessage;

  AddressSpaceChildDriverPingMessage msg;
  msg.metadata = 0;

  EXPECT_TRUE(asd_child.Ping(std::move(msg)).ok());

  {
    auto result = asd_child.ClaimSharedBlock(shared_offset, shared_size);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
  }

  // Test that overlapping blocks cannot be claimed in the same connection.
  for (size_t i = 0; i < overlaps_to_test; ++i) {
    auto result = asd_child.ClaimSharedBlock(overlap_offsets[i], overlap_sizes[i]);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
  }

  {
    auto result = asd_child.UnclaimSharedBlock(shared_offset);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
  }

  // Test that removed or unknown offsets cannot be unclaimed.
  {
    auto result = asd_child.UnclaimSharedBlock(shared_offset);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
  }

  {
    auto result = asd_child.UnclaimSharedBlock(0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
  }
}

// This is a test case testing goldfish Heap, control device, address space
// device, and host implementation of host-visible memory allocation.
//
// This test case using a device-local Heap and a pre-allocated address space
// block to simulate a host-visible sysmem Heap. It does the following things:
//
// 1) It allocates a memory block (vmo = |address_space_vmo| and gpa =
//    |physical_addr|) from address space device.
//
// 2) It allocates an vmo (vmo = |vmo|) from the goldfish device-local Heap
//    so that |vmo| is registered for color buffer creation.
//
// 3) It calls goldfish Control FIDL API to create a color buffer using |vmo|.
//    and maps memory to |physical_addr|.
//
// 4) The color buffer creation and memory process should work correctly, and
//    heap offset should be a non-negative value.
//
TEST(GoldfishHostMemoryTests, GoldfishHostVisibleColorBuffer) {
  // Setup control device.
  int control_device_fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(control_device_fd, 0);

  zx::channel control_channel;
  EXPECT_EQ(fdio_get_service_handle(control_device_fd, control_channel.reset_and_get_address()),
            ZX_OK);

  // ----------------------------------------------------------------------//
  // Setup sysmem allocator and buffer collection.
  zx::channel allocator_client;
  zx::channel allocator_server;
  EXPECT_EQ(zx::channel::create(0, &allocator_client, &allocator_server), ZX_OK);
  EXPECT_EQ(fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator_server.release()),
            ZX_OK);

  llcpp::fuchsia::sysmem::Allocator::SyncClient allocator(std::move(allocator_client));

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator.AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator.BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  // ----------------------------------------------------------------------//
  // Setup address space driver.
  int address_space_fd = open("/dev/class/goldfish-address-space/000", O_RDWR);
  EXPECT_GE(address_space_fd, 0);

  zx::channel parent_channel;
  EXPECT_EQ(fdio_get_service_handle(address_space_fd, parent_channel.reset_and_get_address()),
            ZX_OK);

  zx::channel child_channel;
  zx::channel child_channel2;
  EXPECT_EQ(zx::channel::create(0, &child_channel, &child_channel2), ZX_OK);

  llcpp::fuchsia::hardware::goldfish::AddressSpaceDevice::SyncClient asd_parent(
      std::move(parent_channel));
  {
    auto result = asd_parent.OpenChildDriver(
        llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverType::DEFAULT,
        std::move(child_channel));
    ASSERT_TRUE(result.ok());
  }

  // Allocate device memory block using address space device.
  constexpr uint64_t kHeapSize = 32768ULL;

  llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriver::SyncClient asd_child(
      std::move(child_channel2));
  uint64_t physical_addr = 0;
  zx::vmo address_space_vmo;
  {
    auto result = asd_child.AllocateBlock(kHeapSize);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);

    physical_addr = result.Unwrap()->paddr;
    EXPECT_NE(physical_addr, 0);

    address_space_vmo = std::move(result.Unwrap()->vmo);
    EXPECT_EQ(address_space_vmo.is_valid(), true);
    uint64_t actual_size = 0;
    EXPECT_EQ(address_space_vmo.get_size(&actual_size), ZX_OK);
    EXPECT_GE(actual_size, kHeapSize);
  }

  // ----------------------------------------------------------------------//
  // Use device local heap which only *registers* the koid of vmo to control
  // device.
  llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = llcpp::fuchsia::sysmem::VULKAN_IMAGE_USAGE_TRANSFER_DST;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = llcpp::fuchsia::sysmem::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {llcpp::fuchsia::sysmem::HeapType::GOLDFISH_DEVICE_LOCAL}};

  llcpp::fuchsia::sysmem::BufferCollection::SyncClient collection(std::move(collection_client));
  EXPECT_TRUE(collection.SetConstraints(true, std::move(constraints)).ok());

  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 info;
  {
    auto result = collection.WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->status, ZX_OK);

    info = std::move(result.Unwrap()->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection.Close().ok());

  // ----------------------------------------------------------------------//
  // Creates color buffer and map host memory.
  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  llcpp::fuchsia::hardware::goldfish::ControlDevice::SyncClient control(std::move(control_channel));
  {
    // Verify that a CreateColorBuffer2() call with host-visible memory property,
    // but without physical address will fail.
    auto create_info =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(64))
            .set_height(std::make_unique<uint32_t>(64))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE))
            // Without physical address
            .build();
    auto result = control.CreateColorBuffer2(std::move(vmo_copy), std::move(create_info));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result.Unwrap()->hw_address_page_offset, 0);
  }

  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
  {
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(64))
            .set_height(std::make_unique<uint32_t>(64))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA))
            .set_memory_property(std::make_unique<uint32_t>(0x02u))
            .set_physical_address(std::make_unique<uint64_t>(physical_addr))
            .build();
    auto result = control.CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_GE(result.Unwrap()->hw_address_page_offset, 0);
  }

  // Verify if the color buffer works correctly.
  zx::vmo vmo_copy2;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);
  {
    auto result = control.GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
    EXPECT_NE(result.Unwrap()->id, 0u);
    EXPECT_EQ(result.Unwrap()->type,
              llcpp::fuchsia::hardware::goldfish::BufferHandleType::COLOR_BUFFER);
  }

  // Cleanup.
  {
    auto result = asd_child.DeallocateBlock(physical_addr);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.Unwrap()->res, ZX_OK);
  }
}

int main(int argc, char** argv) {
  if (access("/dev/sys/platform/acpi/goldfish", F_OK) != -1) {
    return zxtest::RunAllTests(argc, argv);
  }
  return 0;
}
