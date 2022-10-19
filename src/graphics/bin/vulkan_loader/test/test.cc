// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/gpu/magma/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fit/defer.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <filesystem>

#include <gtest/gtest.h>

#include "lib/fdio/namespace.h"

// This is the first and only ICD loaded, so it should have a "0-" prepended.
const char* kIcdFilename = "0-libvulkan_fake.so";

zx_status_t ForceWaitForIdle(fuchsia::vulkan::loader::LoaderSyncPtr& loader) {
  zx::vmo vmo;
  // manifest.json remaps this to bin/pkg-server.
  return loader->Get(kIcdFilename, &vmo);
}

TEST(VulkanLoader, ManifestLoad) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));

  zx::vmo vmo_out;
  // manifest.json remaps this to bin/pkg-server.
  EXPECT_EQ(ZX_OK, loader->Get(kIcdFilename, &vmo_out));
  EXPECT_TRUE(vmo_out.is_valid());
  zx_info_handle_basic_t handle_info;
  EXPECT_EQ(ZX_OK, vmo_out.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info),
                                    nullptr, nullptr));
  EXPECT_TRUE(handle_info.rights & ZX_RIGHT_EXECUTE);
  EXPECT_FALSE(handle_info.rights & ZX_RIGHT_WRITE);
  EXPECT_EQ(ZX_OK, loader->Get("not-present", &vmo_out));
  EXPECT_FALSE(vmo_out.is_valid());
}

// Check that writes to one VMO returned by the server will not modify a separate VMO returned by
// the service.
TEST(VulkanLoader, VmosIndependent) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));

  zx::vmo vmo_out;
  // manifest.json remaps this to bin/pkg-server.
  EXPECT_EQ(ZX_OK, loader->Get(kIcdFilename, &vmo_out));
  EXPECT_TRUE(vmo_out.is_valid());

  fzl::VmoMapper mapper;
  EXPECT_EQ(ZX_OK, mapper.Map(vmo_out, 0, 0, ZX_VM_PERM_EXECUTE | ZX_VM_PERM_READ));
  uint8_t original_value = *static_cast<uint8_t*>(mapper.start());
  uint8_t byte_to_write = original_value + 1;
  size_t actual;
  // zx_process_write_memory can write to memory mapped without ZX_VM_PERM_WRITE. If that ever
  // changes, this test can probably be removed.
  zx_status_t status = zx::process::self()->write_memory(
      reinterpret_cast<uint64_t>(mapper.start()), &byte_to_write, sizeof(byte_to_write), &actual);

  // zx_process_write_memory may be disabled using a kernel command-line flag.
  if (status == ZX_ERR_NOT_SUPPORTED) {
    EXPECT_EQ(original_value, *static_cast<uint8_t*>(mapper.start()));
  } else {
    EXPECT_EQ(ZX_OK, status);

    EXPECT_EQ(byte_to_write, *static_cast<uint8_t*>(mapper.start()));
  }

  // Ensure that the new clone is unaffected.
  zx::vmo vmo2;
  EXPECT_EQ(ZX_OK, loader->Get(kIcdFilename, &vmo2));
  EXPECT_TRUE(vmo2.is_valid());

  fzl::VmoMapper mapper2;
  EXPECT_EQ(ZX_OK, mapper2.Map(vmo2, 0, 0, ZX_VM_PERM_EXECUTE | ZX_VM_PERM_READ));
  EXPECT_EQ(original_value, *static_cast<uint8_t*>(mapper2.start()));
}

TEST(VulkanLoader, DeviceFs) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));

  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  EXPECT_EQ(ZX_OK, loader->ConnectToDeviceFs(dir.NewRequest().TakeChannel()));

  ForceWaitForIdle(loader);

  fuchsia::gpu::magma::DeviceSyncPtr device_ptr;
  EXPECT_EQ(ZX_OK, fdio_service_connect_at(dir.channel().get(), "class/gpu/000",
                                           device_ptr.NewRequest().TakeChannel().release()));
  fuchsia::gpu::magma::Device_Query_Result query_result;
  EXPECT_EQ(ZX_OK, device_ptr->Query(fuchsia::gpu::magma::QueryId(0), &query_result));
  ASSERT_TRUE(query_result.is_response());
  ASSERT_TRUE(query_result.response().is_simple_result());
  EXPECT_EQ(5u, query_result.response().simple_result());
}

TEST(VulkanLoader, Features) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));

  fuchsia::vulkan::loader::Features features;
  EXPECT_EQ(ZX_OK, loader->GetSupportedFeatures(&features));
  constexpr fuchsia::vulkan::loader::Features kExpectedFeatures =
      fuchsia::vulkan::loader::Features::CONNECT_TO_DEVICE_FS |
      fuchsia::vulkan::loader::Features::GET |
      fuchsia::vulkan::loader::Features::CONNECT_TO_MANIFEST_FS;
  EXPECT_EQ(kExpectedFeatures, features);
}

TEST(VulkanLoader, ManifestFs) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));

  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  EXPECT_EQ(ZX_OK, loader->ConnectToManifestFs(
                       fuchsia::vulkan::loader::ConnectToManifestOptions::WAIT_FOR_IDLE,
                       dir.NewRequest().TakeChannel()));

  int dir_fd;
  EXPECT_EQ(ZX_OK, fdio_fd_create(dir.TakeChannel().release(), &dir_fd));

  int manifest_fd = openat(dir_fd, (std::string(kIcdFilename) + ".json").c_str(), O_RDONLY);

  EXPECT_LE(0, manifest_fd);

  constexpr int kManifestFileSize = 135;
  char manifest_data[kManifestFileSize + 1];
  ssize_t read_size = read(manifest_fd, manifest_data, sizeof(manifest_data) - 1);
  EXPECT_EQ(kManifestFileSize, read_size);

  close(manifest_fd);
  close(dir_fd);
}

TEST(VulkanLoader, GoldfishSyncDeviceFs) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));

  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  EXPECT_EQ(ZX_OK, loader->ConnectToDeviceFs(dir.NewRequest().TakeChannel()));

  ForceWaitForIdle(loader);

  const char* kDeviceClassList[] = {
      "class/goldfish-sync",
      "class/goldfish-pipe",
      "class/goldfish-address-space",
  };

  for (auto& device_class : kDeviceClassList) {
    fuchsia::io::NodeSyncPtr device_ptr;
    EXPECT_EQ(ZX_OK, fdio_service_connect_at(dir.channel().get(), device_class,
                                             device_ptr.NewRequest().TakeChannel().release()));

    // Check that the directory is connected to something.
    std::vector<uint8_t> protocol;
    zx_status_t status = device_ptr->Query(&protocol);
    EXPECT_EQ(status, ZX_OK) << "class=" << device_class
                             << " status=" << zx_status_get_string(status);
  }
}

TEST(VulkanLoader, DebugFilesystems) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));
  ForceWaitForIdle(loader);

  fuchsia::sys2::RealmQuerySyncPtr query;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.sys2.RealmQuery",
                                        query.NewRequest().TakeChannel().release()));

  fuchsia::sys2::RealmQuery_GetInstanceDirectories_Result result;
  EXPECT_EQ(ZX_OK, query->GetInstanceDirectories("./vulkan_loader", &result));

  fdio_ns_t* ns;
  fdio_ns_get_installed(&ns);
  fdio_ns_bind(ns, "/loader_out",
               result.response().resolved_dirs->execution_dirs->out_dir.channel().get());
  auto cleanup_binding = fit::defer([&]() { fdio_ns_unbind(ns, "/loader_out"); });

  const std::string debug_path("/loader_out/debug/");

  EXPECT_TRUE(std::filesystem::exists(debug_path + "device-fs/class/gpu/000"));
  EXPECT_TRUE(std::filesystem::exists(debug_path + "manifest-fs/" + kIcdFilename + ".json"));
}
