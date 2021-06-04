// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/cpp/fidl_test_base.h>
#include <fuchsia/hardware/goldfish/cpp/fidl_test_base.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

#include "src/graphics/bin/vulkan_loader/app.h"
#include "src/graphics/bin/vulkan_loader/goldfish_device.h"
#include "src/graphics/bin/vulkan_loader/icd_component.h"
#include "src/graphics/bin/vulkan_loader/magma_device.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

class LoaderUnittest : public gtest::RealLoopFixture {};

class FakeMagmaDevice : public fuchsia::gpu::magma::testing::Device_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { EXPECT_TRUE(false) << name; }
  void GetIcdList(GetIcdListCallback callback) override {
    fuchsia::gpu::magma::IcdInfo info;
    info.set_component_url("a");
    info.set_flags(fuchsia::gpu::magma::IcdFlags::SUPPORTS_VULKAN);
    std::vector<fuchsia::gpu::magma::IcdInfo> vec;
    vec.push_back(std::move(info));
    callback(std::move(vec));
  }
  fidl::InterfaceRequestHandler<fuchsia::gpu::magma::Device> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void CloseAll() { bindings_.CloseAll(); }

 private:
  fidl::BindingSet<fuchsia::gpu::magma::Device> bindings_;
};

TEST_F(LoaderUnittest, MagmaDevice) {
  inspect::Inspector inspector;
  auto context = sys::ComponentContext::Create();
  LoaderApp app(context.get(), dispatcher());

  vfs::PseudoDir root;
  FakeMagmaDevice magma_device;
  const char* kDeviceNodeName = "dev";
  root.AddEntry(kDeviceNodeName, std::make_unique<vfs::Service>(magma_device.GetHandler()));
  fidl::InterfaceHandle<fuchsia::io::Directory> pkg_dir;
  async::Loop vfs_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  vfs_loop.StartThread("vfs-loop");
  root.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
             pkg_dir.NewRequest().TakeChannel(), vfs_loop.dispatcher());

  fdio_t* dir_fdio;
  EXPECT_EQ(ZX_OK, fdio_create(pkg_dir.TakeChannel().release(), &dir_fdio));
  int dir_fd = fdio_bind_to_fd(dir_fdio, -1, 0);
  auto device = MagmaDevice::Create(&app, dir_fd, kDeviceNodeName, &inspector.GetRoot());
  EXPECT_TRUE(device);
  auto device_ptr = device.get();

  app.AddDevice(std::move(device));
  RunLoopUntil([&device_ptr]() { return device_ptr->icd_count() > 0; });
  EXPECT_EQ(1u, app.device_count());

  async::PostTask(vfs_loop.dispatcher(), [&magma_device]() { magma_device.CloseAll(); });
  RunLoopUntil([&app]() { return app.device_count() == 0; });
  EXPECT_EQ(0u, app.device_count());

  close(dir_fd);
}

class FakeGoldfishDevice : public fuchsia::hardware::goldfish::testing::PipeDevice_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { EXPECT_TRUE(false) << name; }
  fidl::InterfaceRequestHandler<fuchsia::hardware::goldfish::PipeDevice> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void CloseAll() { bindings_.CloseAll(); }
  size_t GetBindingsSize() { return bindings_.size(); }

 private:
  fidl::BindingSet<fuchsia::hardware::goldfish::PipeDevice> bindings_;
};

TEST_F(LoaderUnittest, GoldfishDevice) {
  inspect::Inspector inspector;
  auto context = sys::ComponentContext::Create();
  LoaderApp app(context.get(), dispatcher());

  vfs::PseudoDir root;
  FakeGoldfishDevice goldfish_device;
  const char* kDeviceNodeName = "dev";
  root.AddEntry(kDeviceNodeName, std::make_unique<vfs::Service>(goldfish_device.GetHandler()));
  fidl::InterfaceHandle<fuchsia::io::Directory> pkg_dir;
  async::Loop vfs_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  vfs_loop.StartThread("vfs-loop");
  root.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
             pkg_dir.NewRequest().TakeChannel(), vfs_loop.dispatcher());

  fdio_t* dir_fdio;
  EXPECT_EQ(ZX_OK, fdio_create(pkg_dir.TakeChannel().release(), &dir_fdio));
  int dir_fd = fdio_bind_to_fd(dir_fdio, -1, 0);
  auto device = GoldfishDevice::Create(&app, dir_fd, kDeviceNodeName, &inspector.GetRoot());
  EXPECT_TRUE(device);
  auto device_ptr = device.get();

  app.AddDevice(std::move(device));
  RunLoopUntil([&device_ptr]() { return device_ptr->icd_count() > 0; });
  EXPECT_EQ(1u, app.device_count());

  async::PostTask(vfs_loop.dispatcher(), [&]() {
    // The request to connect to the goldfish device may still be pending.
    // Remove the "dev" entry to ensure that pending requests are canceled and
    // aren't passed on the FakeGoldfishDevice.
    EXPECT_EQ(ZX_OK, root.RemoveEntry(kDeviceNodeName));
    goldfish_device.CloseAll();
  });
  // Wait until the loader detects that the goldfish device has gone away.
  RunLoopUntil([&app]() { return app.device_count() == 0; });
  EXPECT_EQ(0u, app.device_count());

  close(dir_fd);
  vfs_loop.Shutdown();
  EXPECT_EQ(0u, goldfish_device.GetBindingsSize());
}

TEST(Icd, BadManifest) {
  json::JSONParser parser;
  auto good_doc = parser.ParseFromString(R"({
    "file_path": "bin/pkg-server",
    "library_path": "pkg-server2",
    "version": 1,
    "manifest_path": "data"
})",
                                         "test1");
  EXPECT_TRUE(IcdComponent::ValidateMetadataJson("a", good_doc));

  auto bad_doc1 = parser.ParseFromString(R"({
    "file_path": "bin/pkg-server",
    "library_path": "pkg-server2",
    "version": 2,
    "manifest_path": "data"
})",
                                         "tests2");
  EXPECT_FALSE(IcdComponent::ValidateMetadataJson("b", bad_doc1));

  auto bad_doc2 = parser.ParseFromString(R"({
    "library_path": "pkg-server2",
    "version": 1,
    "manifest_path": "data"
})",
                                         "test3");
  EXPECT_FALSE(IcdComponent::ValidateMetadataJson("c", bad_doc2));

  auto bad_doc3 = parser.ParseFromString(R"({
    "file_path": "bin/pkg-server",
    "library_path": 1,
    "version": 1,
    "manifest_path": "data"
})",
                                         "tests4");
  EXPECT_FALSE(IcdComponent::ValidateMetadataJson("d", bad_doc3));
}
