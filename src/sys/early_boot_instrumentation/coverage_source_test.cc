// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/early_boot_instrumentation/coverage_source.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <sys/stat.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>
#include <sdk/lib/vfs/cpp/flags.h>
#include <sdk/lib/vfs/cpp/pseudo_dir.h>
#include <sdk/lib/vfs/cpp/vmo_file.h>

namespace early_boot_instrumentation {
namespace {

constexpr auto kFlags = fuchsia::io::OPEN_RIGHT_READABLE;

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

// Serve the vmos from /somepath/kernel/vmofile.name.
class FakeBootItemsFixture : public testing::Test {
 public:
  void Serve(const std::string& path) {
    zx::channel dir_server, dir_client;
    ASSERT_EQ(zx::channel::create(0, &dir_server, &dir_client), 0);

    fdio_ns_t* root_ns = nullptr;
    path_ = path;
    ASSERT_EQ(fdio_ns_get_installed(&root_ns), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(root_ns, path.c_str(), dir_client.release()), ZX_OK);

    ASSERT_EQ(kernel_dir_.Serve(kFlags, std::move(dir_server), loop_.dispatcher()), ZX_OK);
    loop_.StartThread("kernel_data_dir");
  }

  void BindFile(std::string_view path) {
    zx::vmo path_vmo;
    ASSERT_EQ(zx::vmo::create(4096, 0, &path_vmo), 0);
    zx_koid_t koid = GetKoid(path_vmo.get());
    ASSERT_NE(koid, ZX_KOID_INVALID);
    auto str_path = std::string(path);
    path_to_koid_[str_path] = koid;

    auto file = std::make_unique<vfs::VmoFile>(std::move(path_vmo), 0, 4096);
    ASSERT_EQ(kernel_dir_.AddEntry(str_path, std::move(file)), ZX_OK);
  }

  void TearDown() override {
    // Best effort.
    fdio_ns_t* root_ns = nullptr;
    ASSERT_EQ(fdio_ns_get_installed(&root_ns), ZX_OK);
    fdio_ns_unbind(root_ns, path_.c_str());
    loop_.Shutdown();
  }

 private:
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  vfs::PseudoDir kernel_dir_;

  std::map<std::string, zx_koid_t> path_to_koid_;
  std::string path_;
};

using ExposeKernelProfileDataTest = FakeBootItemsFixture;
using ExposePhysbootProfileDataTest = FakeBootItemsFixture;

TEST_F(ExposeKernelProfileDataTest, WithSymbolizerLogExposesBoth) {
  BindFile("zircon.elf.profraw");
  BindFile("symbolizer.log");
  ASSERT_NO_FATAL_FAILURE(Serve("/boot/kernel/data"));

  fbl::unique_fd kernel_data_dir(open("/boot/kernel/data", O_RDONLY));
  ASSERT_TRUE(kernel_data_dir) << strerror(errno);

  vfs::PseudoDir out_dir;
  ASSERT_TRUE(ExposeKernelProfileData(kernel_data_dir, out_dir).is_ok());
  ASSERT_FALSE(out_dir.IsEmpty());

  std::string kernel_file(kKernelFile);
  vfs::internal::Node* node = nullptr;
  ASSERT_EQ(out_dir.Lookup(kernel_file, &node), ZX_OK);
  ASSERT_NE(node, nullptr);

  node = nullptr;
  std::string symbolizer_file(kKernelSymbolizerFile);
  ASSERT_EQ(out_dir.Lookup(symbolizer_file, &node), ZX_OK);
  ASSERT_NE(node, nullptr);
}

TEST_F(ExposeKernelProfileDataTest, OnlyKernelFileIsOk) {
  // Dispatcher
  BindFile("zircon.elf.profraw");
  ASSERT_NO_FATAL_FAILURE(Serve("/boot/kernel/data"));

  fbl::unique_fd kernel_data_dir(open("/boot/kernel/data", O_RDONLY));
  ASSERT_TRUE(kernel_data_dir) << strerror(errno);

  vfs::PseudoDir out_dir;
  ASSERT_TRUE(ExposeKernelProfileData(kernel_data_dir, out_dir).is_ok());
  ASSERT_FALSE(out_dir.IsEmpty());

  std::string kernel_file(kKernelFile);
  vfs::internal::Node* node = nullptr;
  ASSERT_EQ(out_dir.Lookup(kernel_file, &node), ZX_OK);
  ASSERT_NE(node, nullptr);

  node = nullptr;
  std::string symbolizer_file(kKernelSymbolizerFile);
  ASSERT_NE(out_dir.Lookup(symbolizer_file, &node), ZX_OK);
}

TEST_F(ExposePhysbootProfileDataTest, WithSymbolizerFileIsOk) {
  // Dispatcher
  BindFile("physboot.profraw");
  BindFile("symbolizer.log");
  ASSERT_NO_FATAL_FAILURE(Serve("/boot/kernel/data/phys"));

  fbl::unique_fd kernel_data_dir(open("/boot/kernel/data/phys", O_RDONLY));
  ASSERT_TRUE(kernel_data_dir) << strerror(errno);

  vfs::PseudoDir out_dir;
  ASSERT_TRUE(ExposePhysbootProfileData(kernel_data_dir, out_dir).is_ok());
  ASSERT_FALSE(out_dir.IsEmpty());

  std::string phys_file(kPhysFile);
  vfs::internal::Node* node = nullptr;
  ASSERT_EQ(out_dir.Lookup(phys_file, &node), ZX_OK);
  ASSERT_NE(node, nullptr);

  node = nullptr;
  std::string symbolizer_file(kPhysSymbolizerFile);
  ASSERT_EQ(out_dir.Lookup(symbolizer_file, &node), ZX_OK);
  ASSERT_NE(node, nullptr);
}

TEST_F(ExposePhysbootProfileDataTest, OnlyProfrawFileIsOk) {
  // Dispatcher
  BindFile("physboot.profraw");
  ASSERT_NO_FATAL_FAILURE(Serve("/boot/kernel/data/phys"));

  fbl::unique_fd kernel_data_dir(open("/boot/kernel/data/phys", O_RDONLY));
  ASSERT_TRUE(kernel_data_dir) << strerror(errno);

  vfs::PseudoDir out_dir;
  ASSERT_TRUE(ExposePhysbootProfileData(kernel_data_dir, out_dir).is_ok());
  ASSERT_FALSE(out_dir.IsEmpty());

  std::string phys_file(kPhysFile);
  vfs::internal::Node* node = nullptr;
  ASSERT_EQ(out_dir.Lookup(phys_file, &node), ZX_OK);
  ASSERT_NE(node, nullptr);

  node = nullptr;
  std::string symbolizer_file(kPhysSymbolizerFile);
  ASSERT_NE(out_dir.Lookup(symbolizer_file, &node), ZX_OK);
}

}  // namespace
}  // namespace early_boot_instrumentation
