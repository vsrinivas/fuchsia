// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CAMERA_MANAGER2_TEST_DEVICE_CREATION_FIXTURE_H_
#define SRC_CAMERA_CAMERA_MANAGER2_TEST_DEVICE_CREATION_FIXTURE_H_

#include <fcntl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

#include "src/lib/syslog/cpp/logger.h"

// This class allows the creation of driver implementations at a given location.
// It is useful when testing services that use device_watcher.
// DevicePath is the path where you want to add devices.
// You can use this fixture in your tests in the following way:
//
// class FakeDevice : public fuchsia::some::Device {
//  // Put implementation here
// };
//
// char path[] = "/dev/class/somedevice";
// typedef DeviceCreationFixture<path, fuchsia::some::Device> CameraDeviceCreationTest;
// TEST_F(CameraDeviceCreationTest, SomeTest) {
//   FakeDevice device;
//   auto dir_ent = AddDevice(&device);  // now that device is added at path!
//   // Run some tests which may open the device
// }
//
template <char const* DevicePath, typename Interface>
class DeviceCreationFixture : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    syslog::InitLogger({"DeviceCreationFixture"});
    vfs_loop_.StartThread("vfs-loop");
    ASSERT_EQ(fdio_ns_get_installed(&ns_), ZX_OK);
    zx::channel c1, c2;

    // Serve up the emulated camera-input directory
    ASSERT_EQ(zx::channel::create(0, &c1, &c2), ZX_OK);
    ASSERT_EQ(vfs_.Serve(dir_, std::move(c1), fs::VnodeConnectionOptions::ReadOnly()), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns_, DevicePath, c2.release()), ZX_OK);
  }

  void TearDown() override {
    ASSERT_TRUE(dir_->IsEmpty());
    vfs_loop_.Shutdown();
    vfs_loop_.JoinThreads();
    ASSERT_NE(ns_, nullptr);
    ASSERT_EQ(fdio_ns_unbind(ns_, DevicePath), ZX_OK);
  }

  // Holds a reference to a pseudo dir entry that removes the entry when this object goes out of
  // scope.
  struct ScopedDirent {
    std::string name;
    fbl::RefPtr<fs::PseudoDir> dir;
    ~ScopedDirent() {
      if (dir) {
        dir->RemoveEntry(name);
      }
    }
  };

  // Adds a device implementation to the emulated directory that has been installed in
  // the local namespace at |DevicePath|.
  [[nodiscard]] ScopedDirent AddDevice(Interface* device) {
    auto name = std::to_string(next_device_number_++);
    auto service = fbl::MakeRefCounted<fs::Service>([device, this](zx::channel c) {
      binding_set_.AddBinding(device, fidl::InterfaceRequest<Interface>(std::move(c)));
      return ZX_OK;
    });
    FXL_CHECK(ZX_OK == dir_->AddEntry(name, service));
    return {name, dir_};
  }

 private:
  fdio_ns_t* ns_ = nullptr;
  uint32_t next_device_number_ = 0;

  async::Loop vfs_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  fs::SynchronousVfs vfs_{vfs_loop_.dispatcher()};
  // Note these _must_ be RefPtrs since the vfs_ will attempt to AdoptRef on a raw pointer passed
  // to it.
  //
  // TODO(35505): Migrate to //sdk/lib/vfs/cpp once that supports watching on PseudoDir.
  fbl::RefPtr<fs::PseudoDir> dir_{fbl::MakeRefCounted<fs::PseudoDir>()};
  fidl::BindingSet<Interface> binding_set_;
};

#endif  // SRC_CAMERA_CAMERA_MANAGER2_TEST_DEVICE_CREATION_FIXTURE_H_
