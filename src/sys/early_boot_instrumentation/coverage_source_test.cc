// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/early_boot_instrumentation/coverage_source.h"

#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/markers.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.debugdata/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/markers.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/message_storage.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/stdcompat/array.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <sys/stat.h>
#include <zircon/syscalls/object.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>
#include <sdk/lib/vfs/cpp/flags.h>
#include <sdk/lib/vfs/cpp/pseudo_dir.h>
#include <sdk/lib/vfs/cpp/vmo_file.h>

namespace early_boot_instrumentation {
namespace {

constexpr auto kFlags = fuchsia::io::OpenFlags::RIGHT_READABLE;

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

    auto file = std::make_unique<vfs::VmoFile>(std::move(path_vmo), 4096);
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

  SinkDirMap sink_map;

  ASSERT_TRUE(ExposeKernelProfileData(kernel_data_dir, sink_map).is_ok());
  vfs::PseudoDir* lookup = nullptr;
  ASSERT_EQ(
      sink_map["llvm-profile"]->Lookup("dynamic", reinterpret_cast<vfs::internal::Node**>(&lookup)),
      ZX_OK);
  vfs::PseudoDir& out_dir = *lookup;

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

  SinkDirMap sink_map;

  ASSERT_TRUE(ExposeKernelProfileData(kernel_data_dir, sink_map).is_ok());
  vfs::PseudoDir* lookup = nullptr;
  ASSERT_EQ(
      sink_map["llvm-profile"]->Lookup("dynamic", reinterpret_cast<vfs::internal::Node**>(&lookup)),
      ZX_OK);
  vfs::PseudoDir& out_dir = *lookup;
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

  SinkDirMap sink_map;

  ASSERT_TRUE(ExposePhysbootProfileData(kernel_data_dir, sink_map).is_ok());
  vfs::PseudoDir* lookup = nullptr;
  ASSERT_EQ(
      sink_map["llvm-profile"]->Lookup("static", reinterpret_cast<vfs::internal::Node**>(&lookup)),
      ZX_OK);
  vfs::PseudoDir& out_dir = *lookup;
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

  SinkDirMap sink_map;

  ASSERT_TRUE(ExposePhysbootProfileData(kernel_data_dir, sink_map).is_ok());
  vfs::PseudoDir* lookup = nullptr;
  ASSERT_EQ(
      sink_map["llvm-profile"]->Lookup("static", reinterpret_cast<vfs::internal::Node**>(&lookup)),
      ZX_OK);
  vfs::PseudoDir& out_dir = *lookup;
  ASSERT_FALSE(out_dir.IsEmpty());

  std::string phys_file(kPhysFile);
  vfs::internal::Node* node = nullptr;
  ASSERT_EQ(out_dir.Lookup(phys_file, &node), ZX_OK);
  ASSERT_NE(node, nullptr);

  node = nullptr;
  std::string symbolizer_file(kPhysSymbolizerFile);
  ASSERT_NE(out_dir.Lookup(symbolizer_file, &node), ZX_OK);
}

struct PublishRequest {
  std::string sink;
  bool peer_closed;
};

constexpr std::string_view kData = "12345670123";
constexpr size_t kDataOffset = 0xAD;

zx::result<zx::vmo> MakeTestVmo(uint32_t data_offset) {
  zx::vmo vmo;
  if (auto status = zx::vmo::create(4096, 0, &vmo); status != ZX_OK) {
    return zx::error(status);
  }
  if (auto status = vmo.write(kData.data(), kDataOffset + data_offset, kDataOffset);
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(vmo));
}

void ValidatePublishedRequests(uint32_t svc_index, cpp20::span<PublishRequest> requests,
                               SinkDirMap& sink_map) {
  for (uint32_t i = 0; i < requests.size(); ++i) {
    std::string path(requests[i].peer_closed ? kStaticDir : kDynamicDir);
    std::string name = std::to_string(svc_index) + "-" + std::to_string(i);
    if (requests[i].sink == kLlvmSink) {
      name += "." + std::string(kLlvmSinkExtension);
    }

    auto it = sink_map.find(requests[i].sink);
    ASSERT_NE(it, sink_map.end());
    auto& sink_root = *it->second;

    vfs::internal::Node* lookup_node = nullptr;
    ASSERT_EQ(sink_root.Lookup(path, &lookup_node), ZX_OK);
    ASSERT_TRUE(lookup_node->IsDirectory());

    auto* typed_dir = reinterpret_cast<vfs::PseudoDir*>(lookup_node);
    ASSERT_EQ(typed_dir->Lookup(name, &lookup_node), ZX_OK) << name;

    auto* vmo_file = reinterpret_cast<vfs::VmoFile*>(lookup_node);
    std::vector<uint8_t> actual_data;
    ASSERT_EQ(vmo_file->ReadAt(kData.size(), kDataOffset + i, &actual_data), ZX_OK);

    EXPECT_TRUE(memcmp(kData.data(), actual_data.data(), kData.size()) == 0);
  }
}

void ValidatePublishedRequests(uint32_t svc_index, PublishRequest& request, SinkDirMap& sink_map) {
  ValidatePublishedRequests(svc_index, {&request, 1}, sink_map);
}

class ExtractDebugDataTest : public ::testing::Test {
 public:
  void SetUp() final {
    zx::channel svc_stash_client;
    ASSERT_EQ(zx::channel::create(0, &svc_stash_read_, &svc_stash_client), ZX_OK);
    fidl::ClientEnd<fuchsia_boot::SvcStash> client_end(std::move(svc_stash_client));
    svc_stash_.Bind(std::move(client_end));
  }

  void StashSvcWithPublishedData(const PublishRequest& publish_info, zx::eventpair& out_token) {
    StashSvcWithPublishedData({&publish_info, 1}, {&out_token, 1});
  }

  // Same as above, but published multiple pairs of |<sink, vmo>| represented by sinks[i], vmos[i].
  // |out| is the write end of the handle.
  void StashSvcWithPublishedData(cpp20::span<const PublishRequest> publish_info,
                                 cpp20::span<zx::eventpair> out_tokens) {
    zx::channel svc_read, svc_write;

    ASSERT_EQ(publish_info.size(), out_tokens.size());

    ASSERT_EQ(zx::channel::create(0, &svc_read, &svc_write), ZX_OK);

    fidl::ServerEnd<fuchsia_io::Directory> dir(std::move(svc_read));
    auto push_result = svc_stash_->Store(std::move(dir));
    ASSERT_TRUE(push_result.ok()) << push_result.status_string();

    for (uint32_t i = 0; i < publish_info.size(); ++i) {
      auto vmo_or = MakeTestVmo(i);
      ASSERT_TRUE(vmo_or.is_ok()) << vmo_or.status_string();
      if (publish_info[i].sink == kLlvmSink) {
        vmo_or.value().set_property(ZX_PROP_NAME, kLlvmSinkExtension.data(),
                                    kLlvmSinkExtension.size());
      }
      PublishOne(svc_write.borrow(), publish_info[i].sink, std::move(vmo_or).value(),
                 out_tokens[i]);
      if (publish_info[i].peer_closed) {
        out_tokens[i].reset();
      }
    }
  }

  auto&& take_stash_read() { return std::move(svc_stash_read_); }

 private:
  static void PublishOne(zx::unowned_channel svc_write, std::string_view sink_name, zx::vmo vmo,
                         zx::eventpair& out_token) {
    zx::channel debugdata_read, debugdata_write;
    ASSERT_EQ(zx::channel::create(0, &debugdata_read, &debugdata_write), ZX_OK);

    zx::eventpair token1, token2;
    ASSERT_EQ(zx::eventpair::create(0, &token1, &token2), ZX_OK);

    // Send an open request on the svc handle.
    ASSERT_EQ(fdio_service_connect_at(svc_write->get(),
                                      fidl::DiscoverableProtocolName<fuchsia_debugdata::Publisher>,
                                      debugdata_read.release()),
              ZX_OK);

    fidl::WireSyncClient<fuchsia_debugdata::Publisher> client;
    fidl::ClientEnd<fuchsia_debugdata::Publisher> client_end(std::move(debugdata_write));
    client.Bind(std::move(client_end));
    auto res = client->Publish(fidl::StringView::FromExternal(sink_name), std::move(vmo),
                               std::move(token1));
    ASSERT_TRUE(res.ok());
    out_token = std::move(token2);
  }

  zx::channel svc_stash_read_;
  fidl::WireSyncClient<fuchsia_boot::SvcStash> svc_stash_;
};

TEST_F(ExtractDebugDataTest, NoRequestsIsEmpty) {
  auto svc_stash = take_stash_read();
  auto sink_map = ExtractDebugData(svc_stash.borrow());
  ASSERT_TRUE(sink_map.empty());
}

TEST_F(ExtractDebugDataTest, SingleStashedSvcWithSingleOutstandingPublishRequest) {
  auto svc_stash = take_stash_read();
  PublishRequest req = {"my-custom-sink", true};
  zx::eventpair token;

  ASSERT_NO_FATAL_FAILURE(StashSvcWithPublishedData(req, token));
  auto sink_map = ExtractDebugData(svc_stash.borrow());
  ASSERT_FALSE(sink_map.empty());
  ValidatePublishedRequests(0u, req, sink_map);
}

TEST_F(ExtractDebugDataTest, LlvmSinkHaveProfrawExtension) {
  auto svc_stash = take_stash_read();
  auto reqs = cpp20::to_array<PublishRequest>(
      {{std::string(kLlvmSink), true}, {std::string(kLlvmSink), false}});
  std::vector<zx::eventpair> tokens;
  tokens.resize(reqs.size());

  ASSERT_NO_FATAL_FAILURE(StashSvcWithPublishedData(reqs, tokens));

  auto sink_map = ExtractDebugData(svc_stash.borrow());
  ASSERT_FALSE(sink_map.empty());

  ValidatePublishedRequests(0u, reqs, sink_map);
}

TEST_F(ExtractDebugDataTest, SingleStashedSvcWithMultipleOutstandingPublishRequest) {
  auto svc_stash = take_stash_read();
  auto reqs = cpp20::to_array<PublishRequest>(
      {{"my-custom-sink", true}, {"another-sink", true}, {"my-custom-sink", false}});
  std::vector<zx::eventpair> tokens;
  tokens.resize(reqs.size());

  ASSERT_NO_FATAL_FAILURE(StashSvcWithPublishedData(reqs, tokens));

  auto sink_map = ExtractDebugData(svc_stash.borrow());
  ASSERT_FALSE(sink_map.empty());

  ValidatePublishedRequests(0u, reqs, sink_map);
}

TEST_F(ExtractDebugDataTest, MultipleStashedSvcWithSingleOutstandingPublishRequest) {
  auto svc_stash = take_stash_read();
  auto reqs = cpp20::to_array<PublishRequest>(
      {{"my-custom-sink", true}, {"another-sink", true}, {"my-custom-sink", false}});
  std::vector<zx::eventpair> tokens;
  tokens.resize(reqs.size());

  for (uint32_t i = 0; i < reqs.size(); ++i) {
    ASSERT_NO_FATAL_FAILURE(StashSvcWithPublishedData(reqs[i], tokens[i]));
  }

  auto sink_map = ExtractDebugData(svc_stash.borrow());
  ASSERT_FALSE(sink_map.empty());

  for (uint32_t i = 0; i < reqs.size(); ++i) {
    ValidatePublishedRequests(i, reqs[i], sink_map);
  }
}

}  // namespace
}  // namespace early_boot_instrumentation
