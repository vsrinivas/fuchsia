// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"

namespace {
fidl::WireSyncClient<fuchsia_sysmem::Allocator> CreateSysmemAllocator() {
  zx::result client_end = component::Connect<fuchsia_sysmem::Allocator>();
  EXPECT_EQ(client_end.status_value(), ZX_OK);
  if (!client_end.is_ok()) {
    return {};
  }
  fidl::WireSyncClient allocator(std::move(*client_end));
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)allocator->SetDebugClientInfo(fidl::StringView::FromExternal(fsl::GetCurrentProcessName()),
                                      fsl::GetCurrentProcessKoid());
  return allocator;
}

void SetDefaultCollectionName(fidl::WireSyncClient<fuchsia_sysmem::BufferCollection>& collection) {
  constexpr uint32_t kTestNamePriority = 1000u;
  std::string test_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
  EXPECT_TRUE(
      collection->SetName(kTestNamePriority, fidl::StringView::FromExternal(test_name)).ok());
}
}  // namespace

TEST(GoldfishPipeTests, GoldfishPipeTest) {
  int fd = open("/dev/class/goldfish-pipe/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  zx::channel pipe_client;
  zx::channel pipe_server;
  EXPECT_EQ(zx::channel::create(0, &pipe_client, &pipe_server), ZX_OK);

  fidl::WireSyncClient<fuchsia_hardware_goldfish::PipeDevice> pipe_device(std::move(channel));
  EXPECT_TRUE(pipe_device->OpenPipe(std::move(pipe_server)).ok());

  fidl::WireSyncClient<fuchsia_hardware_goldfish::Pipe> pipe(std::move(pipe_client));
  const size_t kSize = 3 * 4096;
  {
    auto result = pipe->SetBufferSize(kSize);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
  }

  zx::vmo vmo;
  {
    auto result = pipe->GetBuffer();
    ASSERT_TRUE(result.ok());
    vmo = std::move(result->vmo);
  }

  // Connect to pingpong service.
  constexpr char kPipeName[] = "pipe:pingpong";
  size_t bytes = strlen(kPipeName) + 1;
  EXPECT_EQ(vmo.write(kPipeName, 0, bytes), ZX_OK);

  {
    auto result = pipe->Write(bytes, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_EQ(result->actual, bytes);
  }

  // Write 1 byte.
  const uint8_t kSentinel = 0xaa;
  EXPECT_EQ(vmo.write(&kSentinel, 0, 1), ZX_OK);
  {
    auto result = pipe->Write(1, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_EQ(result->actual, 1U);
  }

  // Read 1 byte result.
  {
    auto result = pipe->Read(1, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_EQ(result->actual, 1U);
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
    auto result = pipe->Write(kSize, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_EQ(result->actual, kSize);
  }

  // Read 3 * 4096 bytes.
  {
    auto result = pipe->Read(kSize, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_EQ(result->actual, kSize);
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
    auto result = pipe->DoCall(kSmallSize, 0u, kSmallSize, kRecvOffset);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_EQ(result->actual, 2 * kSmallSize);
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

  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishDeviceLocal}};

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));

  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, std::move(constraints)).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1U);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection->Close().ok());

  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(std::move(channel));
  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(64)
        .set_height(64)
        .set_format(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
  }

  zx::vmo vmo_copy2;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);

  {
    auto result = control->GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_NE(result->id, 0u);
    EXPECT_EQ(result->type, fuchsia_hardware_goldfish::wire::BufferHandleType::kColorBuffer);
  }

  zx::vmo vmo_copy3;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy3), ZX_OK);

  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(64)
        .set_height(64)
        .set_format(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy3), std::move(create_params));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_ALREADY_EXISTS);
  }
}

TEST(GoldfishControlTests, GoldfishControlTest_HostVisible) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  const size_t kMinSizeBytes = 4 * 1024;
  const size_t kMaxSizeBytes = 4 * 4096;
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = kMinSizeBytes,
      .max_size_bytes = kMaxSizeBytes,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishHostVisible}};
  constraints.image_format_constraints_count = 1;
  constraints.image_format_constraints[0] = fuchsia_sysmem::wire::ImageFormatConstraints{
      .pixel_format =
          fuchsia_sysmem::wire::PixelFormat{
              .type = fuchsia_sysmem::wire::PixelFormatType::kBgra32,
              .has_format_modifier = false,
              .format_modifier = {},
          },
      .color_spaces_count = 1,
      .color_space =
          {
              fuchsia_sysmem::wire::ColorSpace{.type = fuchsia_sysmem::wire::ColorSpaceType::kSrgb},
          },
      .min_coded_width = 32,
      .min_coded_height = 32,
  };

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, std::move(constraints)).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1U);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
    EXPECT_EQ(info.settings.buffer_settings.coherency_domain,
              fuchsia_sysmem::wire::CoherencyDomain::kCpu);
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  uint64_t vmo_size;
  EXPECT_EQ(vmo.get_size(&vmo_size), ZX_OK);
  EXPECT_GE(vmo_size, kMinSizeBytes);
  EXPECT_LE(vmo_size, kMaxSizeBytes);

  // Test if the vmo is mappable.
  zx_vaddr_t addr;
  EXPECT_EQ(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, /*vmar_offset*/ 0, vmo,
                                       /*vmo_offset*/ 0, vmo_size, &addr),
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

  EXPECT_TRUE(collection->Close().ok());
}

TEST(GoldfishControlTests, GoldfishControlTest_HostVisible_MultiClients) {
  using fuchsia_sysmem::BufferCollection;
  using fuchsia_sysmem::wire::BufferCollectionConstraints;

  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  constexpr size_t kNumClients = 2;
  zx::channel token_client[kNumClients];
  zx::channel token_server[kNumClients];
  zx::channel collection_client[kNumClients];
  zx::channel collection_server[kNumClients];

  EXPECT_EQ(zx::channel::create(0, &token_client[0], &token_server[0]), ZX_OK);
  EXPECT_EQ(zx::channel::create(0, &token_client[1], &token_server[1]), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server[0])).ok());

  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)fidl::WireCall<fuchsia_sysmem::BufferCollectionToken>(token_client[0].borrow())
      ->Duplicate(0, std::move(token_server[1]));
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)fidl::WireCall<fuchsia_sysmem::BufferCollectionToken>(token_client[0].borrow())->Sync();

  for (size_t i = 0; i < kNumClients; i++) {
    EXPECT_EQ(zx::channel::create(0, &collection_client[i], &collection_server[i]), ZX_OK);
    EXPECT_TRUE(
        allocator->BindSharedCollection(std::move(token_client[i]), std::move(collection_server[i]))
            .ok());
  }

  const size_t kMinSizeBytes = 4 * 1024;
  const size_t kMaxSizeBytes = 4 * 1024 * 512;
  const size_t kTargetSizeBytes = 4 * 1024 * 512;
  BufferCollectionConstraints constraints[kNumClients];
  for (size_t i = 0; i < kNumClients; i++) {
    constraints[i].usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
    constraints[i].min_buffer_count = 1;
    constraints[i].has_buffer_memory_constraints = true;
    constraints[i].buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = kMinSizeBytes,
        .max_size_bytes = kMaxSizeBytes,
        .physically_contiguous_required = false,
        .secure_required = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
        .inaccessible_domain_supported = false,
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishHostVisible}};
    constraints[i].image_format_constraints_count = 1;
    constraints[i].image_format_constraints[0] = fuchsia_sysmem::wire::ImageFormatConstraints{
        .pixel_format =
            fuchsia_sysmem::wire::PixelFormat{
                .type = fuchsia_sysmem::wire::PixelFormatType::kBgra32,
                .has_format_modifier = false,
                .format_modifier = {},
            },
        .color_spaces_count = 1,
        .color_space =
            {
                fuchsia_sysmem::wire::ColorSpace{.type =
                                                     fuchsia_sysmem::wire::ColorSpaceType::kSrgb},
            },
    };
  }

  // Set different min_coded_width and required_max_coded_width for each client.
  constraints[0].image_format_constraints[0].min_coded_width = 32;
  constraints[0].image_format_constraints[0].min_coded_height = 64;
  constraints[1].image_format_constraints[0].min_coded_width = 16;
  constraints[1].image_format_constraints[0].min_coded_height = 512;
  constraints[1].image_format_constraints[0].required_max_coded_width = 1024;
  constraints[1].image_format_constraints[0].required_max_coded_height = 256;

  fidl::WireSyncClient<BufferCollection> collection[kNumClients];
  for (size_t i = 0; i < kNumClients; i++) {
    collection[i] = fidl::WireSyncClient<BufferCollection>(std::move(collection_client[i])),
    SetDefaultCollectionName(collection[i]);
    EXPECT_TRUE(collection[i]->SetConstraints(true, std::move(constraints[i])).ok());
  };

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection[0]->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1u);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
    EXPECT_EQ(info.settings.buffer_settings.coherency_domain,
              fuchsia_sysmem::wire::CoherencyDomain::kCpu);

    const auto& image_format_constraints =
        result.value().buffer_collection_info.settings.image_format_constraints;

    EXPECT_EQ(image_format_constraints.min_coded_width, 32u);
    EXPECT_EQ(image_format_constraints.min_coded_height, 512u);
    EXPECT_EQ(image_format_constraints.required_max_coded_width, 1024u);
    EXPECT_EQ(image_format_constraints.required_max_coded_height, 256u);

    // Expected coded_width = max(min_coded_width, required_max_coded_width);
    // Expected coded_height = max(min_coded_height, required_max_coded_height).
    // Thus target size should be 1024 x 512 x 4.
    EXPECT_GE(info.settings.buffer_settings.size_bytes, kTargetSizeBytes);
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  uint64_t vmo_size;
  EXPECT_EQ(vmo.get_size(&vmo_size), ZX_OK);
  EXPECT_GE(vmo_size, kTargetSizeBytes);
  EXPECT_LE(vmo_size, kMaxSizeBytes);

  // Test if the vmo is mappable.
  zx_vaddr_t addr;
  EXPECT_EQ(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, /*vmar_offset*/ 0, vmo,
                                       /*vmo_offset*/ 0, vmo_size, &addr),
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

  for (size_t i = 0; i < kNumClients; i++) {
    EXPECT_TRUE(collection[i]->Close().ok());
  }
}

TEST(GoldfishControlTests, GoldfishControlTest_HostVisibleBuffer) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  const size_t kMinSizeBytes = 4 * 1024;
  const size_t kMaxSizeBytes = 4 * 4096;
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = kMinSizeBytes,
      .max_size_bytes = kMaxSizeBytes,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishHostVisible}};
  constraints.image_format_constraints_count = 0;

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, std::move(constraints)).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1U);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
    EXPECT_EQ(info.settings.buffer_settings.coherency_domain,
              fuchsia_sysmem::wire::CoherencyDomain::kCpu);
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  uint64_t vmo_size;
  EXPECT_EQ(vmo.get_size(&vmo_size), ZX_OK);
  EXPECT_GE(vmo_size, kMinSizeBytes);
  EXPECT_LE(vmo_size, kMaxSizeBytes);

  // Test if the vmo is mappable.
  zx_vaddr_t addr;
  EXPECT_EQ(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, /*vmar_offset*/ 0, vmo,
                                       /*vmo_offset*/ 0, vmo_size, &addr),
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

  EXPECT_TRUE(collection->Close().ok());
}

TEST(GoldfishControlTests, GoldfishControlTest_DataBuffer) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  constexpr size_t kBufferSizeBytes = 4 * 1024;
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanBufferUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = kBufferSizeBytes,
      .max_size_bytes = kBufferSizeBytes,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishDeviceLocal}};

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, std::move(constraints)).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1u);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection->Close().ok());

  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(std::move(channel));
  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
    create_params.set_size(allocator, kBufferSizeBytes)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateBuffer2(std::move(vmo_copy), std::move(create_params));
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->is_ok());
  }

  zx::vmo vmo_copy2;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);

  {
    auto result = control->GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_NE(result->id, 0u);
    EXPECT_EQ(result->type, fuchsia_hardware_goldfish::wire::BufferHandleType::kBuffer);
  }

  zx::vmo vmo_copy3;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy3), ZX_OK);

  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(64)
        .set_height(64)
        .set_format(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy3), std::move(create_params));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_ALREADY_EXISTS);
  }

  zx::vmo vmo_copy4;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy4), ZX_OK);

  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
    create_params.set_size(allocator, kBufferSizeBytes)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateBuffer2(std::move(vmo_copy4), std::move(create_params));
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_ALREADY_EXISTS);
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

  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(std::move(channel));
  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(16)
        .set_height(16)
        .set_format(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
  }

  // Call GetBufferHandle() using vmo not registered with goldfish
  // sysmem heap.
  zx::vmo vmo_copy2;
  EXPECT_EQ(non_sysmem_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);

  {
    auto result = control->GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
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
  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  // ----------------------------------------------------------------------//
  // Use device local heap which only *registers* the koid of vmo to control
  // device.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishDeviceLocal}};

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, std::move(constraints)).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1u);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection->Close().ok());

  // ----------------------------------------------------------------------//
  // Try creating color buffer.
  zx::vmo vmo_copy;
  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(
      std::move(control_channel));

  {
    // Verify that a CreateColorBuffer2() call without width will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // Without width
    create_params.set_height(64)
        .set_format(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result->hw_address_page_offset, 0);
  }

  {
    // Verify that a CreateColorBuffer2() call without height will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // Without height
    create_params.set_width(64)
        .set_format(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result->hw_address_page_offset, 0);
  }

  {
    // Verify that a CreateColorBuffer2() call without color format will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // Without format
    create_params.set_width(64).set_height(64).set_memory_property(
        fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result->hw_address_page_offset, 0);
  }

  {
    // Verify that a CreateColorBuffer2() call without memory property will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // Without memory property
    create_params.set_width(64).set_height(64).set_format(
        fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result->hw_address_page_offset, 0);
  }
}

// In this test case we test arguments of CreateBuffer2() method.
// If a mandatory field is missing, it should return "ZX_ERR_INVALID_ARGS".
TEST(GoldfishControlTests, GoldfishControlTest_CreateBuffer2Args) {
  // Setup control device.
  int control_device_fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(control_device_fd, 0);

  zx::channel control_channel;
  EXPECT_EQ(fdio_get_service_handle(control_device_fd, control_channel.reset_and_get_address()),
            ZX_OK);

  // ----------------------------------------------------------------------//
  // Setup sysmem allocator and buffer collection.
  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  // ----------------------------------------------------------------------//
  // Use device local heap which only *registers* the koid of vmo to control
  // device.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishDeviceLocal}};

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, std::move(constraints)).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1u);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection->Close().ok());

  // ----------------------------------------------------------------------//
  // Try creating data buffers.
  zx::vmo vmo_copy;
  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(
      std::move(control_channel));

  {
    // Verify that a CreateBuffer2() call without width will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
    // Without size
    create_params.set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }

  {
    // Verify that a CreateBuffer2() call without memory property will fail.
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
    // Without memory property
    create_params.set_size(allocator, 4096);
    auto result = control->CreateBuffer2(std::move(vmo_copy), std::move(create_params));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
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

  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishDeviceLocal}};

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, constraints).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1u);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection->Close().ok());

  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(std::move(channel));
  {
    auto result = control->GetBufferHandle(std::move(vmo_copy));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_NOT_FOUND);
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

  fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceDevice> asd_parent(
      std::move(parent_channel));
  {
    auto result = asd_parent->OpenChildDriver(
        fuchsia_hardware_goldfish::wire::AddressSpaceChildDriverType::kDefault,
        std::move(child_channel));
    ASSERT_TRUE(result.ok());
  }

  constexpr uint64_t kHeapSize = 16ULL * 1048576ULL;

  fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceChildDriver> asd_child(
      std::move(child_channel2));
  uint64_t paddr = 0;
  zx::vmo vmo;
  {
    auto result = asd_child->AllocateBlock(kHeapSize);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);

    paddr = result->paddr;
    EXPECT_NE(paddr, 0U);

    vmo = std::move(result->vmo);
    EXPECT_EQ(vmo.is_valid(), true);
    uint64_t actual_size = 0;
    EXPECT_EQ(vmo.get_size(&actual_size), ZX_OK);
    EXPECT_GE(actual_size, kHeapSize);
  }

  zx::vmo vmo2;
  uint64_t paddr2 = 0;
  {
    auto result = asd_child->AllocateBlock(kHeapSize);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);

    paddr2 = result->paddr;
    EXPECT_NE(paddr2, 0U);
    EXPECT_NE(paddr2, paddr);

    vmo2 = std::move(result->vmo);
    EXPECT_EQ(vmo2.is_valid(), true);
    uint64_t actual_size = 0;
    EXPECT_EQ(vmo2.get_size(&actual_size), ZX_OK);
    EXPECT_GE(actual_size, kHeapSize);
  }

  {
    auto result = asd_child->DeallocateBlock(paddr);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
  }

  {
    auto result = asd_child->DeallocateBlock(paddr2);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
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

  using fuchsia_hardware_goldfish::wire::AddressSpaceChildDriverPingMessage;

  AddressSpaceChildDriverPingMessage msg;
  msg.metadata = 0;

  EXPECT_TRUE(asd_child->Ping(msg).ok());

  {
    auto result = asd_child->ClaimSharedBlock(shared_offset, shared_size);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
  }

  // Test that overlapping blocks cannot be claimed in the same connection.
  for (size_t i = 0; i < overlaps_to_test; ++i) {
    auto result = asd_child->ClaimSharedBlock(overlap_offsets[i], overlap_sizes[i]);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
  }

  {
    auto result = asd_child->UnclaimSharedBlock(shared_offset);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
  }

  // Test that removed or unknown offsets cannot be unclaimed.
  {
    auto result = asd_child->UnclaimSharedBlock(shared_offset);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
  }

  {
    auto result = asd_child->UnclaimSharedBlock(0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
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
  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

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

  fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceDevice> asd_parent(
      std::move(parent_channel));
  {
    auto result = asd_parent->OpenChildDriver(
        fuchsia_hardware_goldfish::wire::AddressSpaceChildDriverType::kDefault,
        std::move(child_channel));
    ASSERT_TRUE(result.ok());
  }

  // Allocate device memory block using address space device.
  constexpr uint64_t kHeapSize = 32768ULL;

  fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceChildDriver> asd_child(
      std::move(child_channel2));
  uint64_t physical_addr = 0;
  zx::vmo address_space_vmo;
  {
    auto result = asd_child->AllocateBlock(kHeapSize);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);

    physical_addr = result->paddr;
    EXPECT_NE(physical_addr, 0U);

    address_space_vmo = std::move(result->vmo);
    EXPECT_EQ(address_space_vmo.is_valid(), true);
    uint64_t actual_size = 0;
    EXPECT_EQ(address_space_vmo.get_size(&actual_size), ZX_OK);
    EXPECT_GE(actual_size, kHeapSize);
  }

  // ----------------------------------------------------------------------//
  // Use device local heap which only *registers* the koid of vmo to control
  // device.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishDeviceLocal}};

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, constraints).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1U);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection->Close().ok());

  // ----------------------------------------------------------------------//
  // Creates color buffer and map host memory.
  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(
      std::move(control_channel));
  {
    // Verify that a CreateColorBuffer2() call with host-visible memory property,
    // but without physical address will fail.
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // Without physical address
    create_params.set_width(64)
        .set_height(64)
        .set_format(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra)
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), create_params);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_ERR_INVALID_ARGS);
    EXPECT_LT(result->hw_address_page_offset, 0);
  }

  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);
  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(64)
        .set_height(64)
        .set_format(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra)
        .set_memory_property(0x02u)
        .set_physical_address(allocator, physical_addr);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), create_params);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_GE(result->hw_address_page_offset, 0);
  }

  // Verify if the color buffer works correctly.
  zx::vmo vmo_copy2;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);
  {
    auto result = control->GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_NE(result->id, 0u);
    EXPECT_EQ(result->type, fuchsia_hardware_goldfish::wire::BufferHandleType::kColorBuffer);
  }

  // Cleanup.
  {
    auto result = asd_child->DeallocateBlock(physical_addr);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
  }
}

using GoldfishCreateColorBufferTest =
    testing::TestWithParam<fuchsia_hardware_goldfish::wire::ColorBufferFormatType>;

TEST_P(GoldfishCreateColorBufferTest, CreateColorBufferWithFormat) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  auto allocator = CreateSysmemAllocator();
  EXPECT_TRUE(allocator.is_valid());

  zx::channel token_client;
  zx::channel token_server;
  EXPECT_EQ(zx::channel::create(0, &token_client, &token_server), ZX_OK);
  EXPECT_TRUE(allocator->AllocateSharedCollection(std::move(token_server)).ok());

  zx::channel collection_client;
  zx::channel collection_server;
  EXPECT_EQ(zx::channel::create(0, &collection_client, &collection_server), ZX_OK);
  EXPECT_TRUE(
      allocator->BindSharedCollection(std::move(token_client), std::move(collection_server)).ok());

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kGoldfishDeviceLocal}};

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  SetDefaultCollectionName(collection);
  EXPECT_TRUE(collection->SetConstraints(true, constraints).ok());

  fuchsia_sysmem::wire::BufferCollectionInfo2 info;
  {
    auto result = collection->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->status, ZX_OK);

    info = std::move(result->buffer_collection_info);
    EXPECT_EQ(info.buffer_count, 1U);
    EXPECT_TRUE(info.buffers[0].vmo.is_valid());
  }

  zx::vmo vmo = std::move(info.buffers[0].vmo);
  EXPECT_TRUE(vmo.is_valid());

  EXPECT_TRUE(collection->Close().ok());

  zx::vmo vmo_copy;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy), ZX_OK);

  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(std::move(channel));
  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(64)
        .set_height(64)
        .set_format(GetParam())
        .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

    auto result = control->CreateColorBuffer2(std::move(vmo_copy), create_params);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
  }

  zx::vmo vmo_copy2;
  EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy2), ZX_OK);

  {
    auto result = control->GetBufferHandle(std::move(vmo_copy2));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->res, ZX_OK);
    EXPECT_NE(result->id, 0u);
    EXPECT_EQ(result->type, fuchsia_hardware_goldfish::wire::BufferHandleType::kColorBuffer);
  }
}

TEST(GoldfishControlTests, CreateSyncKhr) {
  int fd = open("/dev/class/goldfish-control/000", O_RDWR);
  EXPECT_GE(fd, 0);

  zx::channel channel;
  EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK);

  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> control(std::move(channel));

  zx::eventpair event_client, event_server;
  zx_status_t status = zx::eventpair::create(0u, &event_client, &event_server);
  {
    auto result = control->CreateSyncFence(std::move(event_server));
    ASSERT_TRUE(result.ok());
  }

  zx_signals_t pending;
  status = event_client.wait_one(ZX_EVENTPAIR_SIGNALED, zx::deadline_after(zx::sec(10)), &pending);
  EXPECT_EQ(status, ZX_OK);
}

INSTANTIATE_TEST_SUITE_P(
    ColorBufferTests, GoldfishCreateColorBufferTest,
    testing::Values(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRgba,
                    fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra,
                    fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRg,
                    fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kLuminance),
    [](const testing::TestParamInfo<GoldfishCreateColorBufferTest::ParamType>& info)
        -> std::string {
      switch (info.param) {
        case fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRgba:
          return "RGBA";
        case fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra:
          return "BGRA";
        case fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRg:
          return "RG";
        case fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kLuminance:
          return "LUMINANCE";
      }
    });
