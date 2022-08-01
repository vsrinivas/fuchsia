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

class ExposeEarlyBootStashedProfileDataTest : public ::testing::Test {
 public:
  void SetUp() final {
    zx::channel svc_stash_client;
    ASSERT_EQ(zx::channel::create(0, &svc_stash_read_, &svc_stash_client), ZX_OK);
    fidl::ClientEnd<fuchsia_boot::SvcStash> client_end(std::move(svc_stash_client));
    svc_stash_.Bind(std::move(client_end));
  }

  // Publish |vmo| with |sink_name| into a svc directory, whose handle is moved to |out|.
  // The read side of the |/svc| handle, is stashed into |svc_stash_write|, so it can be read
  // by |svc_stash_read| handle.
  void AddSvcWith(std::string_view sink_name, zx::vmo vmo, zx::eventpair& out_token) {
    zx::channel svc_read, svc_write;
    ASSERT_EQ(zx::channel::create(0, &svc_read, &svc_write), ZX_OK);

    fidl::ServerEnd<fuchsia_io::Directory> dir(std::move(svc_read));
    auto push_result = svc_stash_->Store(std::move(dir));
    ASSERT_TRUE(push_result.ok()) << push_result.status_string();

    PublishOne(svc_write.borrow(), sink_name, std::move(vmo), out_token);
  }

  // Same as above, but published multiple pairs of |<sink, vmo>| represented by sinks[i], vmos[i].
  // |out| is the write end of the handle.
  void AddSvcWithMany(cpp20::span<std::string_view> sinks, cpp20::span<zx::vmo> vmos,
                      cpp20::span<zx::eventpair> out_tokens) {
    zx::channel svc_read, svc_write;

    ASSERT_EQ(sinks.size(), vmos.size());
    ASSERT_EQ(sinks.size(), out_tokens.size());
    ASSERT_EQ(zx::channel::create(0, &svc_read, &svc_write), ZX_OK);

    fidl::ServerEnd<fuchsia_io::Directory> dir(std::move(svc_read));
    auto push_result = svc_stash_->Store(std::move(dir));
    ASSERT_TRUE(push_result.ok()) << push_result.status_string();

    for (size_t i = 0; i < sinks.size(); ++i) {
      PublishOne(svc_write.borrow(), sinks[i], std::move(vmos[i]), out_tokens[i]);
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

constexpr std::string_view kData = "12345670123";
constexpr size_t kDataOffset = 0xAD;
constexpr std::string_view kSink = "llvm-profile";
constexpr std::string_view kUnhandledSink = "not-llvm-profile";

zx::status<zx::vmo> MakeTestVmo(uint32_t data_offset) {
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

void MakeTestVmos(cpp20::span<zx::vmo> vmos) {
  for (uint32_t i = 0; i < vmos.size(); ++i) {
    vmos[i] = std::move(MakeTestVmo(i).value());
  }
}

struct EntryContext {
  std::vector<std::string_view> sinks;
  bool peer_closed;
};

void ValidateVmos(cpp20::span<EntryContext> entries, vfs::PseudoDir& static_dir,
                  vfs::PseudoDir& dynamic_dir) {
  for (uint32_t svc_index = 0; svc_index < entries.size(); ++svc_index) {
    auto& entry = entries[svc_index];
    auto& sinks = entry.sinks;
    for (uint32_t vmo_index = 0, unhandled_count = 0; vmo_index < entry.sinks.size(); ++vmo_index) {
      if (sinks[vmo_index] == kUnhandledSink) {
        unhandled_count++;
        continue;
      }

      auto& out_dir = entry.peer_closed ? static_dir : dynamic_dir;

      // Should be added to the static dir.
      vfs::internal::Node* node = nullptr;

      // This is the first and only one in the stash.
      std::string exposed_as = std::to_string(svc_index) + "-" +
                               std::to_string(vmo_index - unhandled_count) + ".profraw";

      ASSERT_EQ(out_dir.Lookup(exposed_as, &node), ZX_OK);
      ASSERT_NE(node, nullptr);
      ASSERT_TRUE(node->IsVMO());
      auto* vmo_node = reinterpret_cast<vfs::VmoFile*>(node);
      std::vector<uint8_t> file_contents(kData.size(), 0);
      ASSERT_EQ(vmo_node->ReadAt(file_contents.size(), kDataOffset + vmo_index, &file_contents),
                ZX_OK);

      EXPECT_TRUE(memcmp(kData.data(), file_contents.data(), kData.size()) == 0);
    }
  }
}

void ValidateVmo(std::string_view name, bool peer_closed, vfs::PseudoDir& static_dir,
                 vfs::PseudoDir& dynamic_dir) {
  EntryContext entry = {
      .sinks = {name},
      .peer_closed = peer_closed,
  };
  ValidateVmos({&entry, 1}, static_dir, dynamic_dir);
}

TEST_F(ExposeEarlyBootStashedProfileDataTest, SinglePublisherWithOneElement) {
  vfs::PseudoDir static_dir, dynamic_dir;
  zx::vmo published_vmo = MakeTestVmo(0).value();
  zx::eventpair local_token;

  AddSvcWith(kSink, std::move(published_vmo), local_token);

  auto svc_stash = take_stash_read();

  ExposeEarlyBootStashedProfileData(svc_stash.borrow(), dynamic_dir, static_dir);

  ValidateVmo(kSink, false, static_dir, dynamic_dir);
  ASSERT_TRUE(static_dir.IsEmpty());
}

TEST_F(ExposeEarlyBootStashedProfileDataTest, SinglePublisherWithOneElementAndSvcPeerClosed) {
  vfs::PseudoDir static_dir, dynamic_dir;
  zx::vmo published_vmo = MakeTestVmo(0).value();
  zx::eventpair local_token;

  AddSvcWith(kSink, std::move(published_vmo), local_token);

  // Close the peer.
  local_token.reset();

  auto svc_stash = take_stash_read();

  ExposeEarlyBootStashedProfileData(svc_stash.borrow(), dynamic_dir, static_dir);

  ValidateVmo(kSink, true, static_dir, dynamic_dir);
  ASSERT_TRUE(dynamic_dir.IsEmpty());
}

TEST_F(ExposeEarlyBootStashedProfileDataTest, SinglePublisherWithMultipleElement) {
  vfs::PseudoDir static_dir, dynamic_dir;
  constexpr std::string_view kName = "llvm-profile";
  constexpr std::string_view kUnhandledName = "not-llvm-profile";
  constexpr uint32_t kVmoCount = 4;

  std::array<zx::vmo, kVmoCount> published_vmos;
  std::array<zx::eventpair, kVmoCount> local_token;
  EntryContext entry = {
      .sinks = {kName, kName, kUnhandledName, kName},
      .peer_closed = false,
  };

  MakeTestVmos(published_vmos);
  AddSvcWithMany(entry.sinks, published_vmos, local_token);

  auto svc_stash = take_stash_read();

  ExposeEarlyBootStashedProfileData(svc_stash.borrow(), dynamic_dir, static_dir);

  ValidateVmos({&entry, 1}, static_dir, dynamic_dir);
  ASSERT_TRUE(static_dir.IsEmpty());
}

TEST_F(ExposeEarlyBootStashedProfileDataTest, SinglePublisherWithMultipleElementAndTokenSignaled) {
  vfs::PseudoDir static_dir, dynamic_dir;
  constexpr std::string_view kName = "llvm-profile";
  constexpr std::string_view kUnhandledName = "not-llvm-profile";
  constexpr uint32_t kVmoCount = 4;

  std::array<zx::vmo, kVmoCount> published_vmos;
  std::array<zx::eventpair, kVmoCount> local_tokens;
  EntryContext entry = {
      .sinks = {kName, kName, kUnhandledName, kName},
      .peer_closed = true,
  };

  MakeTestVmos(published_vmos);
  AddSvcWithMany(entry.sinks, published_vmos, local_tokens);

  for (auto& token : local_tokens) {
    token.reset();
  }

  auto svc_stash = take_stash_read();

  ExposeEarlyBootStashedProfileData(svc_stash.borrow(), dynamic_dir, static_dir);

  ValidateVmos({&entry, 1}, static_dir, dynamic_dir);
  ASSERT_TRUE(dynamic_dir.IsEmpty());
}

TEST_F(ExposeEarlyBootStashedProfileDataTest, MultiplePublisherWithSingleElement) {
  constexpr uint32_t kPublisherCount = 3;

  vfs::PseudoDir static_dir, dynamic_dir;
  std::array<zx::eventpair, kPublisherCount> local_tokens;
  std::array<EntryContext, kPublisherCount> entries = {{
      {
          .sinks = {kSink},
          .peer_closed = false,
      },
      {
          .sinks = {kSink},
          .peer_closed = false,
      },
      {
          .sinks = {kSink},
          .peer_closed = false,
      },
  }};

  for (size_t i = 0; i < kPublisherCount; ++i) {
    for (size_t j = 0; j < entries[i].sinks.size(); ++j) {
      zx::vmo published_vmo = MakeTestVmo(0).value();
      AddSvcWith(entries[i].sinks[j], std::move(published_vmo), local_tokens[i]);
    }
  }

  auto svc_stash = take_stash_read();

  ExposeEarlyBootStashedProfileData(svc_stash.borrow(), dynamic_dir, static_dir);

  ValidateVmos(entries, static_dir, dynamic_dir);
  ASSERT_TRUE(static_dir.IsEmpty());
}

TEST_F(ExposeEarlyBootStashedProfileDataTest, MultiplePublisherWithSingleElementAndTokenSignaled) {
  constexpr uint32_t kPublisherCount = 3;

  vfs::PseudoDir static_dir, dynamic_dir;
  std::array<zx::eventpair, kPublisherCount> local_tokens;
  std::array<EntryContext, kPublisherCount> entries = {{
      {
          .sinks = {kSink},
          .peer_closed = true,
      },
      {
          .sinks = {kSink},
          .peer_closed = true,
      },
      {
          .sinks = {kSink},
          .peer_closed = true,
      },
  }};

  for (size_t i = 0; i < kPublisherCount; ++i) {
    for (size_t j = 0; j < entries[i].sinks.size(); ++j) {
      zx::vmo published_vmo = MakeTestVmo(0).value();
      AddSvcWith(entries[i].sinks[j], std::move(published_vmo), local_tokens[i]);
    }
  }

  auto svc_stash = take_stash_read();
  for (auto& svc : local_tokens) {
    svc.reset();
  }

  ExposeEarlyBootStashedProfileData(svc_stash.borrow(), dynamic_dir, static_dir);

  ValidateVmos(entries, static_dir, dynamic_dir);
  ASSERT_TRUE(dynamic_dir.IsEmpty());
}

TEST_F(ExposeEarlyBootStashedProfileDataTest, MultiplePublisherWithMultipleElements) {
  constexpr uint32_t kPublisherCount = 3;

  vfs::PseudoDir static_dir, dynamic_dir;
  std::array<std::vector<zx::eventpair>, kPublisherCount> local_tokens;
  std::array<EntryContext, kPublisherCount> entries = {{
      {
          .sinks = {kSink, kSink, kUnhandledSink},
          .peer_closed = false,
      },
      {
          .sinks = {kUnhandledSink, kUnhandledSink},
          .peer_closed = false,
      },
      {
          .sinks = {kSink, kSink},
          .peer_closed = false,
      },
  }};

  for (size_t i = 0; i < kPublisherCount; ++i) {
    std::vector<zx::vmo> published_vmos(entries[i].sinks.size());
    local_tokens[i].resize(entries[i].sinks.size());
    MakeTestVmos(published_vmos);
    AddSvcWithMany(entries[i].sinks, published_vmos, local_tokens[i]);
  }

  auto svc_stash = take_stash_read();

  ExposeEarlyBootStashedProfileData(svc_stash.borrow(), dynamic_dir, static_dir);

  ValidateVmos(entries, static_dir, dynamic_dir);
  ASSERT_TRUE(static_dir.IsEmpty());
}

TEST_F(ExposeEarlyBootStashedProfileDataTest,
       MultiplePublisherWithMultipleElementAndTokenSignaled) {
  constexpr uint32_t kPublisherCount = 3;

  vfs::PseudoDir static_dir, dynamic_dir;
  std::array<std::vector<zx::eventpair>, kPublisherCount> local_tokens;
  std::array<EntryContext, kPublisherCount> entries = {{
      {
          .sinks = {kSink, kSink, kUnhandledSink},
          .peer_closed = true,
      },
      {
          .sinks = {kUnhandledSink, kUnhandledSink},
          .peer_closed = true,
      },
      {
          .sinks = {kSink, kSink},
          .peer_closed = true,
      },
  }};

  for (size_t i = 0; i < kPublisherCount; ++i) {
    std::vector<zx::vmo> published_vmos(entries[i].sinks.size());
    local_tokens[i].resize(entries[i].sinks.size());
    MakeTestVmos(published_vmos);
    AddSvcWithMany(entries[i].sinks, published_vmos, local_tokens[i]);
  }

  for (auto& token_list : local_tokens) {
    for (auto& token : token_list) {
      token.reset();
    }
  }

  auto svc_stash = take_stash_read();

  ExposeEarlyBootStashedProfileData(svc_stash.borrow(), dynamic_dir, static_dir);

  ValidateVmos(entries, static_dir, dynamic_dir);
  ASSERT_TRUE(dynamic_dir.IsEmpty());
}

}  // namespace
}  // namespace early_boot_instrumentation
