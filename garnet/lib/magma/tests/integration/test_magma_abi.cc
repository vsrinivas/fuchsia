// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>

#include <array>
#include <thread>

#if defined(__Fuchsia__)
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include "fuchsia/sysmem/cpp/fidl.h"
#include "magma_sysmem.h"
#include "platform_logger.h"
#include "platform_trace_provider.h"
#endif

#include "gtest/gtest.h"
#include "magma.h"
#include "magma_common_defs.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"

extern "C" {
#include "test_magma_abi.h"
}

class TestConnection {
 public:
  static constexpr const char* kDeviceNameFuchsia = "/dev/class/gpu/000";
  static constexpr const char* kDeviceNameVirt = "/dev/magma0";

#if defined(__Fuchsia__)
  static const char* device_name() { return kDeviceNameFuchsia; }
#elif defined(__linux__)
  static const char* device_name() { return kDeviceNameVirt; }
#else
#error Unimplemented
#endif

  static bool is_virtmagma() { return device_name() == kDeviceNameVirt; }

  TestConnection() {
#if defined(__Fuchsia__)
    zx::channel server_end, client_end;
    zx::channel::create(0, &server_end, &client_end);
    EXPECT_EQ(ZX_OK, fdio_service_connect(device_name(), server_end.release()));

    EXPECT_EQ(MAGMA_STATUS_OK, magma_device_import(client_end.release(), &device_));
#elif defined(__linux__)
    int fd = open(device_name(), O_RDONLY);
    DASSERT(fd >= 0);
    EXPECT_EQ(MAGMA_STATUS_OK, magma_device_import(fd, &device_));
#else
#error Unimplemented
#endif
    magma_create_connection2(device_, &connection_);
  }

  ~TestConnection() {
    if (connection_)
      magma_release_connection(connection_);

    if (fd_ >= 0)
      close(fd_);
    if (device_)
      magma_device_release(device_);
  }

  int fd() { return fd_; }

  magma_connection_t connection() { return connection_; }

  void Connection() { ASSERT_NE(connection_, nullptr); }

  void Context() {
    if (is_virtmagma())
      GTEST_SKIP();

    ASSERT_NE(connection_, nullptr);

    uint32_t context_id[2];

    magma_create_context(connection_, &context_id[0]);
    EXPECT_EQ(magma_get_error(connection_), 0);

    magma_create_context(connection_, &context_id[1]);
    EXPECT_EQ(magma_get_error(connection_), 0);

    magma_release_context(connection_, context_id[0]);
    EXPECT_EQ(magma_get_error(connection_), 0);

    magma_release_context(connection_, context_id[1]);
    EXPECT_EQ(magma_get_error(connection_), 0);

    magma_release_context(connection_, context_id[1]);
    EXPECT_NE(magma_get_error(connection_), 0);
  }

  void NotificationChannelHandle() {
    uint32_t handle = magma_get_notification_channel_handle(connection_);
    EXPECT_NE(0u, handle);

    uint32_t handle2 = magma_get_notification_channel_handle(connection_);
    EXPECT_EQ(handle, handle2);
  }

  void WaitNotificationChannel() {
    constexpr uint64_t kOneSecondInNs = 1000000000;
    magma_status_t status = magma_wait_notification_channel(connection_, kOneSecondInNs);
    EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, status);
  }

  void ReadNotificationChannel() {
    std::array<unsigned char, 1024> buffer;
    uint64_t buffer_size = ~0;
    magma_status_t status =
        magma_read_notification_channel(connection_, buffer.data(), buffer.size(), &buffer_size);
    EXPECT_EQ(MAGMA_STATUS_OK, status);
    EXPECT_EQ(0u, buffer_size);
  }

  void Buffer() {
    ASSERT_NE(connection_, nullptr);

    uint64_t size = magma::page_size();
    uint64_t actual_size;
    uint64_t id;

    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_buffer(connection_, size, &actual_size, &id));
    EXPECT_GE(size, actual_size);
    EXPECT_NE(id, 0u);

    magma_release_buffer(connection_, id);
  }

  void BufferMap() {
    ASSERT_NE(connection_, nullptr);

    uint64_t size = magma::page_size();
    uint64_t actual_size;
    uint64_t id;

    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_buffer(connection_, size, &actual_size, &id));
    EXPECT_NE(id, 0u);

    magma_map_buffer_gpu(connection_, id, 1024, 0, size / magma::page_size(),
                         MAGMA_GPU_MAP_FLAG_READ);
    magma_unmap_buffer_gpu(connection_, id, 2048);
    EXPECT_NE(MAGMA_STATUS_OK, magma_get_error(connection_));
    EXPECT_EQ(MAGMA_STATUS_MEMORY_ERROR, magma_commit_buffer(connection_, id, 100, 100));
    EXPECT_EQ(MAGMA_STATUS_OK, magma_get_error(connection_));

    magma_release_buffer(connection_, id);
  }

  void BufferExport(uint32_t* handle_out, uint64_t* id_out) {
    ASSERT_NE(connection_, nullptr);

    uint64_t size = magma::page_size();
    magma_buffer_t buffer;

    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_buffer(connection_, size, &size, &buffer));

    *id_out = magma_get_buffer_id(buffer);

    EXPECT_EQ(MAGMA_STATUS_OK, magma_export(connection_, buffer, handle_out));
  }

  void BufferRelease() {
    if (is_virtmagma())
      GTEST_SKIP();

    uint64_t id;
    uint32_t handle;
    BufferExport(&handle, &id);
    EXPECT_EQ(MAGMA_STATUS_OK, magma_release_buffer_handle(handle));
  }

  void BufferImport(uint32_t handle, uint64_t id) {
    ASSERT_NE(connection_, nullptr);

    magma_buffer_t buffer;
    ASSERT_EQ(MAGMA_STATUS_OK, magma_import(connection_, handle, &buffer));
    EXPECT_EQ(magma_get_buffer_id(buffer), id);
  }

  static void BufferImportExport(TestConnection* test1, TestConnection* test2) {
    if (is_virtmagma())
      GTEST_SKIP();

    uint32_t handle;
    uint64_t id;
    test1->BufferExport(&handle, &id);
    test2->BufferImport(handle, id);
  }

  void Semaphore(uint32_t count) {
    if (is_virtmagma())
      GTEST_SKIP();

    ASSERT_NE(connection_, nullptr);

    std::vector<magma_semaphore_t> semaphore(count);
    for (uint32_t i = 0; i < count; i++) {
      ASSERT_EQ(MAGMA_STATUS_OK, magma_create_semaphore(connection_, &semaphore[i]));
      EXPECT_NE(0u, magma_get_semaphore_id(semaphore[i]));
    }

    auto thread = std::thread([semaphore, wait_all = true] {
      ASSERT_EQ(MAGMA_STATUS_OK, magma_wait_semaphores(semaphore.data(), semaphore.size(),
                                                       magma::ms_to_signed_ns(5000), wait_all));
      for (uint32_t i = 0; i < semaphore.size(); i++) {
        magma_reset_semaphore(semaphore[i]);
      }
      EXPECT_EQ(MAGMA_STATUS_TIMED_OUT,
                magma_wait_semaphores(semaphore.data(), semaphore.size(), 100, wait_all));
    });

    for (uint32_t i = 0; i < semaphore.size(); i++) {
      usleep(10 * 1000);
      magma_signal_semaphore(semaphore[i]);
    }
    thread.join();

    thread = std::thread([semaphore, wait_all = false] {
      ASSERT_EQ(MAGMA_STATUS_OK, magma_wait_semaphores(semaphore.data(), semaphore.size(),
                                                       magma::ms_to_signed_ns(5000), wait_all));
      for (uint32_t i = 0; i < semaphore.size(); i++) {
        magma_reset_semaphore(semaphore[i]);
      }
      EXPECT_EQ(MAGMA_STATUS_TIMED_OUT,
                magma_wait_semaphores(semaphore.data(), semaphore.size(), 100, wait_all));
    });

    usleep(10 * 1000);
    magma_signal_semaphore(semaphore[semaphore.size() - 1]);
    thread.join();

    for (uint32_t i = 0; i < semaphore.size(); i++) {
      magma_release_semaphore(connection_, semaphore[i]);
    }
  }

  void SemaphoreExport(uint32_t* handle_out, uint64_t* id_out) {
    ASSERT_NE(connection_, nullptr);
    magma_semaphore_t semaphore;

    ASSERT_EQ(magma_create_semaphore(connection_, &semaphore), MAGMA_STATUS_OK);
    *id_out = magma_get_semaphore_id(semaphore);
    EXPECT_EQ(magma_export_semaphore(connection_, semaphore, handle_out), MAGMA_STATUS_OK);
  }

  void SemaphoreImport(uint32_t handle, uint64_t id) {
    ASSERT_NE(connection_, nullptr);
    magma_semaphore_t semaphore;

    ASSERT_EQ(magma_import_semaphore(connection_, handle, &semaphore), MAGMA_STATUS_OK);
    EXPECT_EQ(magma_get_semaphore_id(semaphore), id);
  }

  static void SemaphoreImportExport(TestConnection* test1, TestConnection* test2) {
    if (is_virtmagma())
      GTEST_SKIP();

    uint32_t handle;
    uint64_t id;
    test1->SemaphoreExport(&handle, &id);
    test2->SemaphoreImport(handle, id);
  }

  void ImmediateCommands() {
    uint32_t context_id;
    magma_create_context(connection_, &context_id);
    EXPECT_EQ(magma_get_error(connection_), 0);

    magma_inline_command_buffer inline_command_buffer{};
    magma_execute_immediate_commands2(connection_, context_id, 0, &inline_command_buffer);
    EXPECT_EQ(magma_get_error(connection_), 0);

    magma_release_context(connection_, context_id);
    EXPECT_EQ(magma_get_error(connection_), 0);
  }

  void ImageFormat() {
#if !defined(__Fuchsia__)
    GTEST_SKIP();
#else
    fuchsia::sysmem::SingleBufferSettings buffer_settings;
    buffer_settings.has_image_format_constraints = true;
    buffer_settings.image_format_constraints.pixel_format.type =
        fuchsia::sysmem::PixelFormatType::NV12;
    buffer_settings.image_format_constraints.min_bytes_per_row = 128;
    buffer_settings.image_format_constraints.bytes_per_row_divisor = 256;
    buffer_settings.image_format_constraints.min_coded_height = 64;
    fidl::Encoder encoder(fidl::Encoder::NO_HEADER);
    encoder.Alloc(
        fidl::EncodingInlineSize<fuchsia::sysmem::SingleBufferSettings, fidl::Encoder>(&encoder));
    buffer_settings.Encode(&encoder, 0);
    std::vector<uint8_t> encoded_bytes = encoder.TakeBytes();
    size_t real_size = encoded_bytes.size();
    // Add an extra byte to ensure the size is correct.
    encoded_bytes.push_back(0);
    magma_buffer_format_description_t description;
    ASSERT_EQ(MAGMA_STATUS_OK,
              magma_get_buffer_format_description(encoded_bytes.data(), real_size, &description));
    magma_image_plane_t planes[4];
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_get_buffer_format_plane_info_with_size(description, 128u, 64u, planes));

    EXPECT_EQ(256u, planes[0].bytes_per_row);
    EXPECT_EQ(0u, planes[0].byte_offset);
    EXPECT_EQ(256u, planes[1].bytes_per_row);
    EXPECT_EQ(256u * 64u, planes[1].byte_offset);
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_get_buffer_format_plane_info_with_size(description, 128u, 64u, planes));
    EXPECT_EQ(256u, planes[0].bytes_per_row);
    EXPECT_EQ(0u, planes[0].byte_offset);
    EXPECT_EQ(256u, planes[1].bytes_per_row);
    EXPECT_EQ(256u * 64u, planes[1].byte_offset);
    magma_buffer_format_description_release(description);
    EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, magma_get_buffer_format_description(
                                             encoded_bytes.data(), real_size + 1, &description));
    EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, magma_get_buffer_format_description(
                                             encoded_bytes.data(), real_size - 1, &description));
#endif
  }

  void Sysmem(bool use_format_modifier) {
#if !defined(__Fuchsia__)
    GTEST_SKIP();
#else
    magma_sysmem_connection_t connection;
    zx::channel local_endpoint, server_endpoint;
    EXPECT_EQ(ZX_OK, zx::channel::create(0u, &local_endpoint, &server_endpoint));
    EXPECT_EQ(ZX_OK,
              fdio_service_connect("/svc/fuchsia.sysmem.Allocator", server_endpoint.release()));
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_sysmem_connection_import(local_endpoint.release(), &connection));

    magma_buffer_collection_t collection;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_buffer_collection_import(connection, ZX_HANDLE_INVALID, &collection));

    magma_buffer_format_constraints_t buffer_constraints{};

    buffer_constraints.count = 1;
    buffer_constraints.usage = 0;
    buffer_constraints.secure_permitted = false;
    buffer_constraints.secure_required = false;
    buffer_constraints.cpu_domain_supported = true;
    magma_sysmem_buffer_constraints_t constraints;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_buffer_constraints_create(connection, &buffer_constraints, &constraints));

    // Create a set of basic 512x512 RGBA image constraints.
    magma_image_format_constraints_t image_constraints{};
    image_constraints.image_format = MAGMA_FORMAT_R8G8B8A8;
    image_constraints.has_format_modifier = use_format_modifier;
    image_constraints.format_modifier = use_format_modifier ? MAGMA_FORMAT_MODIFIER_LINEAR : 0;
    image_constraints.width = 512;
    image_constraints.height = 512;
    image_constraints.layers = 1;
    image_constraints.bytes_per_row_divisor = 1;
    image_constraints.min_bytes_per_row = 0;

    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_buffer_constraints_set_format(connection, constraints, 0, &image_constraints));

    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_buffer_collection_set_constraints(connection, collection, constraints));

    // Buffer should be allocated now.
    magma_buffer_format_description_t description;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_sysmem_get_description_from_collection(connection, collection, &description));

    uint32_t buffer_count;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_get_buffer_count(description, &buffer_count));
    EXPECT_EQ(1u, buffer_count);
    magma_bool_t is_secure;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_get_buffer_is_secure(description, &is_secure));
    EXPECT_FALSE(is_secure);

    magma_bool_t has_format_modifier;
    uint64_t format_modifier;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_get_buffer_format_modifier(description, &has_format_modifier,
                                                                &format_modifier));
    if (has_format_modifier) {
      EXPECT_EQ(MAGMA_FORMAT_MODIFIER_LINEAR, format_modifier);
    }

    magma_image_plane_t planes[4];
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_get_buffer_format_plane_info_with_size(description, 512u, 512u, planes));
    EXPECT_EQ(512 * 4u, planes[0].bytes_per_row);
    EXPECT_EQ(0u, planes[0].byte_offset);
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_get_buffer_format_plane_info_with_size(description, 512, 512, planes));
    EXPECT_EQ(512 * 4u, planes[0].bytes_per_row);
    EXPECT_EQ(0u, planes[0].byte_offset);

    uint32_t handle;
    uint32_t offset;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_sysmem_get_buffer_handle_from_collection(
                                   connection, collection, 0, &handle, &offset));
    EXPECT_EQ(MAGMA_STATUS_OK, magma_release_buffer_handle(handle));

    magma_buffer_collection_release(connection, collection);
    magma_buffer_constraints_release(connection, constraints);
    magma_sysmem_connection_release(connection);
#endif
  }

  void TracingInit() {
#if !defined(__Fuchsia__)
    GTEST_SKIP();
#else
    zx::channel local_endpoint, server_endpoint;
    EXPECT_EQ(ZX_OK, zx::channel::create(0u, &local_endpoint, &server_endpoint));
    EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.tracing.provider.Registry",
                                          server_endpoint.release()));
    EXPECT_EQ(MAGMA_STATUS_OK, magma_initialize_tracing(local_endpoint.release()));

    if (magma::PlatformTraceProvider::Get())
      EXPECT_TRUE(magma::PlatformTraceProvider::Get()->IsInitialized());
#endif
  }

  void LoggingInit() {
#if !defined(__Fuchsia__)
    GTEST_SKIP();
#else
    zx::channel local_endpoint, server_endpoint;
    EXPECT_EQ(ZX_OK, zx::channel::create(0u, &local_endpoint, &server_endpoint));
    EXPECT_EQ(ZX_OK,
              fdio_service_connect("/svc/fuchsia.logger.LogSink", server_endpoint.release()));
    EXPECT_EQ(MAGMA_STATUS_OK, magma_initialize_logging(local_endpoint.release()));
    EXPECT_TRUE(magma::PlatformLogger::IsInitialized());
#endif
  }

  void GetDeviceIdImported() {
    uint64_t device_id = 0;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_query2(device_, MAGMA_QUERY_DEVICE_ID, &device_id));
    EXPECT_NE(0u, device_id);
  }

  void GetVendorIdImported() {
    uint64_t vendor_id = 0;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_query2(device_, MAGMA_QUERY_VENDOR_ID, &vendor_id));
    EXPECT_NE(0u, vendor_id);
  }

  void QueryReturnsBufferImported() {
    uint32_t handle_out = 0;
    // Drivers shouldn't allow this value to be queried through this entrypoint.
    EXPECT_NE(MAGMA_STATUS_OK,
              magma_query_returns_buffer2(device_, MAGMA_QUERY_DEVICE_ID, &handle_out));
    EXPECT_EQ(0u, handle_out);
  }

 private:
  int fd_ = -1;
  magma_device_t device_ = 0;
  magma_connection_t connection_;
};

class TestConnectionWithContext : public TestConnection {
 public:
  TestConnectionWithContext() { magma_create_context(connection(), &context_id_); }

  ~TestConnectionWithContext() { magma_release_context(connection(), context_id_); }

  uint32_t context_id() { return context_id_; }

  void ExecuteCommandBufferWithResources(uint32_t resource_count) {
    magma_system_command_buffer command_buffer = {.num_resources = resource_count};
    magma_system_exec_resource resources[resource_count];

    memset(resources, 0, sizeof(magma_system_exec_resource) * resource_count);
    magma_execute_command_buffer_with_resources(connection(), context_id(), &command_buffer,
                                                resources, nullptr);

    // Command buffer is mostly zeros, so we expect an error here
    EXPECT_NE(MAGMA_STATUS_OK, magma_get_error(connection()));
  }

 private:
  uint32_t context_id_;
};

TEST(MagmaAbi, DeviceId) {
  TestConnection test;
  test.GetDeviceIdImported();
}

TEST(MagmaAbi, VendorId) {
  TestConnection test;
  test.GetVendorIdImported();
}

TEST(MagmaAbi, QueryReturnsBuffer) {
  TestConnection test;
  test.QueryReturnsBufferImported();
}

TEST(MagmaAbi, TracingInit) {
  TestConnection test;
  test.TracingInit();
}

TEST(MagmaAbi, LoggingInit) {
  TestConnection test;
  test.LoggingInit();
}

TEST(MagmaAbi, Buffer) {
  TestConnection test;
  test.Buffer();
}

TEST(MagmaAbi, Connection) {
  TestConnection test;
  test.Connection();
}

TEST(MagmaAbi, Context) {
  TestConnection test;
  test.Context();
}

TEST(MagmaAbi, NotificationChannelHandle) {
  TestConnection test;
  test.NotificationChannelHandle();
}

TEST(MagmaAbi, ReadNotificationChannel) {
  TestConnection test;
  test.ReadNotificationChannel();
}

TEST(MagmaAbi, BufferMap) {
  TestConnection test;
  test.BufferMap();
}

TEST(MagmaAbi, BufferRelease) {
  TestConnection test;
  test.BufferRelease();
}

TEST(MagmaAbi, BufferImportExport) {
  TestConnection test1;
  TestConnection test2;
  TestConnection::BufferImportExport(&test1, &test2);
}

TEST(MagmaAbi, Semaphore) {
  TestConnection test;
  test.Semaphore(1);
  test.Semaphore(2);
  test.Semaphore(3);
}

TEST(MagmaAbi, SemaphoreImportExport) {
  TestConnection test1;
  TestConnection test2;
  TestConnection::SemaphoreImportExport(&test1, &test2);
}

TEST(MagmaAbi, ImmediateCommands) { TestConnection().ImmediateCommands(); }

TEST(MagmaAbi, ImageFormat) {
  TestConnection test;
  test.ImageFormat();
}

TEST(MagmaAbi, Sysmem) {
  TestConnection test;
  test.Sysmem(false);
}

TEST(MagmaAbi, SysmemLinearFormatModifier) {
  TestConnection test;
  test.Sysmem(true);
}

TEST(MagmaAbi, FromC) { EXPECT_TRUE(test_magma_abi_from_c(TestConnection::device_name())); }

TEST(MagmaAbi, ExecuteCommandBufferWithResources) {
  TestConnectionWithContext().ExecuteCommandBufferWithResources(5);
}

TEST(MagmaAbiPerf, ExecuteCommandBufferWithResources) {
  if (TestConnection::is_virtmagma())
    GTEST_SKIP();

  TestConnectionWithContext test;

  auto start = std::chrono::steady_clock::now();
  constexpr uint32_t kTestIterations = 10000;
  for (uint32_t test_iter = kTestIterations; test_iter; --test_iter) {
    test.ExecuteCommandBufferWithResources(10);
  }

  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start);

  magma::log(magma::LOG_INFO, "ExecuteCommandBufferWithResources: avg duration %lld ns",
             duration.count() / kTestIterations);
}
