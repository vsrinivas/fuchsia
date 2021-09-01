// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>

#include <atomic>
#include <string_view>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {

namespace fio = fuchsia_io;

zx_status_t DummyReader(fbl::String* output) { return ZX_OK; }

zx_status_t DummyWriter(std::string_view input) { return ZX_OK; }

// Example vnode that supports protocol negotiation. Here the vnode may be opened as a file or a
// directory.
class FileOrDirectory : public fs::Vnode {
 public:
  FileOrDirectory() = default;

  fs::VnodeProtocolSet GetProtocols() const final {
    return fs::VnodeProtocol::kFile | fs::VnodeProtocol::kDirectory;
  }

  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights,
                                     fs::VnodeRepresentation* info) final {
    switch (protocol) {
      case fs::VnodeProtocol::kFile:
        *info = fs::VnodeRepresentation::File();
        break;
      case fs::VnodeProtocol::kDirectory:
        *info = fs::VnodeRepresentation::Directory();
        break;
      default:
        ZX_ASSERT_MSG(false, "Unreachable");
    }
    return ZX_OK;
  }
};

// Helper method to monitor the OnOpen event, used by the tests below when
// OPEN_FLAG_DESCRIBE is used.
zx::status<fio::wire::NodeInfo> GetOnOpenResponse(zx::unowned_channel channel) {
  zx::status<fio::wire::NodeInfo> node_info{};
  auto get_on_open_response = [](zx::unowned_channel channel,
                                 zx::status<fio::wire::NodeInfo>& node_info) {
    class EventHandler final : public fidl::WireSyncEventHandler<fio::Node> {
     public:
      explicit EventHandler() = default;

      void OnOpen(fidl::WireResponse<fio::Node::OnOpen>* event) override {
        ASSERT_NE(event, nullptr);
        response_ = *event;
      }

      fidl::WireResponse<fio::Node::OnOpen> GetResponse() { return response_; }

      zx_status_t Unknown() override { return ZX_ERR_UNAVAILABLE; }

     private:
      fidl::WireResponse<fio::Node::OnOpen> response_;
    };

    EventHandler event_handler{};
    fidl::Result event_result = event_handler.HandleOneEvent(channel);
    // Expect that |on_open| was received
    ASSERT_TRUE(event_result.ok());
    fidl::WireResponse<fio::Node::OnOpen> response = event_handler.GetResponse();
    if (response.s != ZX_OK) {
      node_info = zx::error(response.s);
      return;
    }
    ASSERT_FALSE(response.info.has_invalid_tag());
    node_info = zx::ok(response.info);
  };
  get_on_open_response(std::move(channel), node_info);
  return node_info;
}

class VfsTestSetup : public zxtest::Test {
 public:
  // Setup file structure with one directory and one file. Note: On creation directories and files
  // have no flags and rights.
  VfsTestSetup() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_.SetDispatcher(loop_.dispatcher());
    root_ = fbl::MakeRefCounted<fs::PseudoDir>();
    dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
    file_ = fbl::MakeRefCounted<fs::BufferedPseudoFile>(&DummyReader, &DummyWriter);
    file_or_dir_ = fbl::MakeRefCounted<FileOrDirectory>();
    root_->AddEntry("dir", dir_);
    root_->AddEntry("file", file_);
    root_->AddEntry("file_or_dir", file_or_dir_);
  }

  zx_status_t ConnectClient(zx::channel server_end) {
    // Serve root directory with maximum rights
    return vfs_.ServeDirectory(root_, std::move(server_end));
  }

 protected:
  void SetUp() override { loop_.StartThread(); }

  void TearDown() override { loop_.Shutdown(); }

 private:
  async::Loop loop_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;
  fbl::RefPtr<fs::PseudoDir> dir_;
  fbl::RefPtr<fs::Vnode> file_;
  fbl::RefPtr<FileOrDirectory> file_or_dir_;
};

using ConnectionTest = VfsTestSetup;

TEST_F(ConnectionTest, NodeGetSetFlagsOnFile) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to File
  zx::channel fc1, fc2;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::wire::kOpenRightReadable, fc2.release()));

  // Use NodeGetFlags to get current flags and rights
  auto file_get_result = fidl::WireCall<fio::Node>(zx::unowned_channel(fc1)).NodeGetFlags();
  EXPECT_OK(file_get_result.status());
  EXPECT_EQ(fio::wire::kOpenRightReadable, file_get_result.Unwrap()->flags);
  {
    // Make modifications to flags with NodeSetFlags: Note this only works for kOpenFlagAppend
    // based on posix standard
    auto file_set_result = fidl::WireCall<fio::Node>(zx::unowned_channel(fc1))
                               .NodeSetFlags(fio::wire::kOpenFlagAppend);
    EXPECT_OK(file_set_result.Unwrap()->s);
  }
  {
    // Check that the new flag is saved
    auto file_get_result = fidl::WireCall<fio::Node>(zx::unowned_channel(fc1)).NodeGetFlags();
    EXPECT_OK(file_get_result.Unwrap()->s);
    EXPECT_EQ(fio::wire::kOpenRightReadable | fio::wire::kOpenFlagAppend,
              file_get_result.Unwrap()->flags);
  }
}

TEST_F(ConnectionTest, NodeGetSetFlagsOnDirectory) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to Directory
  zx::channel dc1, dc2;
  ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
  ASSERT_OK(fdio_open_at(client_end.get(), "dir",
                         fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable,
                         dc2.release()));

  // Read/write/read directory flags; same as for file
  auto dir_get_result = fidl::WireCall<fio::Node>(zx::unowned_channel(dc1)).NodeGetFlags();
  EXPECT_OK(dir_get_result.Unwrap()->s);
  EXPECT_EQ(fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable,
            dir_get_result.Unwrap()->flags);

  auto dir_set_result =
      fidl::WireCall<fio::Node>(zx::unowned_channel(dc1)).NodeSetFlags(fio::wire::kOpenFlagAppend);
  EXPECT_OK(dir_set_result.Unwrap()->s);

  auto dir_get_result_2 = fidl::WireCall<fio::Node>(zx::unowned_channel(dc1)).NodeGetFlags();
  EXPECT_OK(dir_get_result_2.Unwrap()->s);
  EXPECT_EQ(
      fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable | fio::wire::kOpenFlagAppend,
      dir_get_result_2.Unwrap()->flags);
}

TEST_F(ConnectionTest, PosixFlagDirectoryRightExpansion) {
  // Create connection to VFS with all rights.
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Combinations of POSIX flags to be tested.
  // TODO(fxbug.dev/40862): Remove OPEN_FLAG_POSIX.
  const uint32_t OPEN_FLAG_COMBINATIONS[]{
      fio::wire::kOpenFlagPosixWritable, fio::wire::kOpenFlagPosixExecutable,
      fio::wire::kOpenFlagPosixWritable | fio::wire::kOpenFlagPosixExecutable,
      fio::wire::kOpenFlagPosix};

  for (const uint32_t OPEN_FLAGS : OPEN_FLAG_COMBINATIONS) {
    // Connect to drectory specifying the flag combination we want to test.
    zx::channel dc1, dc2;
    ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
    ASSERT_OK(fdio_open_at(client_end.get(), "dir", fio::wire::kOpenRightReadable | OPEN_FLAGS,
                           dc2.release()));

    // Ensure flags match those which we expect.
    auto dir_get_result = fidl::WireCall<fio::Node>(zx::unowned_channel(dc1)).NodeGetFlags();
    EXPECT_OK(dir_get_result.Unwrap()->s);
    auto dir_flags = dir_get_result.Unwrap()->flags;
    EXPECT_NE(fio::wire::kOpenRightReadable & dir_flags, 0);
    // Each POSIX flag should be expanded to its respective right(s).
    if (OPEN_FLAGS & (fio::wire::kOpenFlagPosix | fio::wire::kOpenFlagPosixWritable))
      EXPECT_NE(fio::wire::kOpenRightWritable & dir_flags, 0);
    if (OPEN_FLAGS & (fio::wire::kOpenFlagPosix | fio::wire::kOpenFlagPosixExecutable))
      EXPECT_NE(fio::wire::kOpenRightExecutable & dir_flags, 0);

    // Repeat test, but for file, which should not have any expanded rights.
    zx::channel fc1, fc2;
    ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
    ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::wire::kOpenRightReadable | OPEN_FLAGS,
                           fc2.release()));
    auto file_get_result = fidl::WireCall<fio::File>(zx::unowned_channel(fc1)).GetFlags();
    EXPECT_OK(file_get_result.status());
    EXPECT_EQ(fio::wire::kOpenRightReadable, file_get_result.Unwrap()->flags);
  }
}

TEST_F(ConnectionTest, FileGetSetFlagsOnFile) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to File
  zx::channel fc1, fc2;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::wire::kOpenRightReadable, fc2.release()));

  {
    // Use NodeGetFlags to get current flags and rights
    auto file_get_result = fidl::WireCall<fio::File>(zx::unowned_channel(fc1)).GetFlags();
    EXPECT_OK(file_get_result.status());
    EXPECT_EQ(fio::wire::kOpenRightReadable, file_get_result.Unwrap()->flags);
  }
  {
    // Make modifications to flags with NodeSetFlags: Note this only works for kOpenFlagAppend
    // based on posix standard
    auto file_set_result =
        fidl::WireCall<fio::File>(zx::unowned_channel(fc1)).SetFlags(fio::wire::kOpenFlagAppend);
    EXPECT_OK(file_set_result.Unwrap()->s);
  }
  {
    // Check that the new flag is saved
    auto file_get_result = fidl::WireCall<fio::File>(zx::unowned_channel(fc1)).GetFlags();
    EXPECT_OK(file_get_result.Unwrap()->s);
    EXPECT_EQ(fio::wire::kOpenRightReadable | fio::wire::kOpenFlagAppend,
              file_get_result.Unwrap()->flags);
  }
}

TEST_F(ConnectionTest, FileGetSetFlagsDirectory) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Read/write flags on a Directory connection using File protocol Get/SetFlags should fail.
  {
    zx::channel dc1, dc2;
    ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
    ASSERT_OK(fdio_open_at(client_end.get(), "dir",
                           fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable,
                           dc2.release()));

    auto dir_get_result = fidl::WireCall<fio::File>(zx::unowned_channel(dc1)).GetFlags();
    EXPECT_NOT_OK(dir_get_result.status());
  }

  {
    zx::channel dc1, dc2;
    ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
    ASSERT_OK(fdio_open_at(client_end.get(), "dir",
                           fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable,
                           dc2.release()));

    auto dir_set_result =
        fidl::WireCall<fio::File>(zx::unowned_channel(dc1)).SetFlags(fio::wire::kOpenFlagAppend);
    EXPECT_NOT_OK(dir_set_result.status());
  }
}

TEST_F(ConnectionTest, NegotiateProtocol) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  constexpr uint32_t kOpenMode = 0755;

  // Connect to polymorphic node as a directory, by passing |kOpenFlagDirectory|.
  zx::channel dc1, dc2;
  ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
  ASSERT_OK(fidl::WireCall<fio::Directory>(zx::unowned_channel(client_end))
                .Open(fio::wire::kOpenRightReadable | fio::wire::kOpenFlagDescribe |
                          fio::wire::kOpenFlagDirectory,
                      kOpenMode, fidl::StringView("file_or_dir"), std::move(dc2))
                .status());
  zx::status<fio::wire::NodeInfo> dir_info = GetOnOpenResponse(zx::unowned_channel(dc1));
  ASSERT_OK(dir_info);
  ASSERT_TRUE(dir_info->is_directory());

  // Connect to polymorphic node as a file, by passing |kOpenFlagNotDirectory|.
  zx::channel fc1, fc2;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
  ASSERT_OK(fidl::WireCall<fio::Directory>(zx::unowned_channel(client_end))
                .Open(fio::wire::kOpenRightReadable | fio::wire::kOpenFlagDescribe |
                          fio::wire::kOpenFlagNotDirectory,
                      kOpenMode, fidl::StringView("file_or_dir"), std::move(fc2))
                .status());
  zx::status<fio::wire::NodeInfo> file_info = GetOnOpenResponse(zx::unowned_channel(fc1));
  ASSERT_OK(file_info);
  ASSERT_TRUE(file_info->is_file());
}

TEST_F(ConnectionTest, PrevalidateFlagsOpenFailure) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  constexpr uint32_t kOpenMode = 0755;
  // Flag combination which should return INVALID_ARGS (see PrevalidateFlags in connection.cc).
  constexpr uint32_t kInvalidFlagCombo =
      fio::wire::kOpenRightReadable | fio::wire::kOpenFlagDescribe | fio::wire::kOpenFlagDirectory |
      fio::wire::kOpenFlagNodeReference | fio::wire::kOpenFlagAppend;
  zx::channel dc1, dc2;
  ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
  // Ensure that invalid flag combination returns INVALID_ARGS.
  ASSERT_OK(fidl::WireCall<fio::Directory>(zx::unowned_channel(client_end))
                .Open(kInvalidFlagCombo, kOpenMode, fidl::StringView("file_or_dir"), std::move(dc2))
                .status());
  ASSERT_EQ(GetOnOpenResponse(zx::unowned_channel(dc1)).status_value(), ZX_ERR_INVALID_ARGS);
}

// A vnode which maintains a counter of number of |Open| calls that have not been balanced out with
// a |Close|.
class CountOutstandingOpenVnode : public fs::Vnode {
 public:
  CountOutstandingOpenVnode() = default;

  fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kFile; }

  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final {
    *info = fs::VnodeRepresentation::File{};
    return ZX_OK;
  }

  uint64_t GetOpenCount() const {
    std::lock_guard lock(mutex_);
    return open_count();
  }
};

class ConnectionClosingTest : public zxtest::Test {
 public:
  // Setup file structure with one directory and one file. Note: On creation directories and files
  // have no flags and rights.
  ConnectionClosingTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_.SetDispatcher(loop_.dispatcher());
    root_ = fbl::MakeRefCounted<fs::PseudoDir>();
    count_outstanding_open_vnode_ = fbl::MakeRefCounted<CountOutstandingOpenVnode>();
    root_->AddEntry("count_outstanding_open_vnode", count_outstanding_open_vnode_);
  }

  zx_status_t ConnectClient(zx::channel server_end) {
    // Serve root directory with maximum rights
    return vfs_.ServeDirectory(root_, std::move(server_end));
  }

 protected:
  fbl::RefPtr<CountOutstandingOpenVnode> count_outstanding_open_vnode() const {
    return count_outstanding_open_vnode_;
  }

  async::Loop& loop() { return loop_; }

 private:
  async::Loop loop_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;
  fbl::RefPtr<CountOutstandingOpenVnode> count_outstanding_open_vnode_;
};

TEST_F(ConnectionClosingTest, ClosingChannelImpliesClosingNode) {
  // Create connection to vfs.
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  constexpr uint32_t kOpenMode = 0755;
  constexpr int kNumActiveClients = 20;

  ASSERT_EQ(count_outstanding_open_vnode()->GetOpenCount(), 0);

  // Create a number of active connections to "count_outstanding_open_vnode".
  std::vector<zx::channel> clients;
  for (int i = 0; i < kNumActiveClients; i++) {
    zx::channel fc1, fc2;
    ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
    ASSERT_OK(fidl::WireCall<fio::Directory>(zx::unowned_channel(client_end))
                  .Open(fio::wire::kOpenRightReadable, kOpenMode,
                        fidl::StringView("count_outstanding_open_vnode"), std::move(fc2))
                  .status());
    clients.push_back(std::move(fc1));
  }

  ASSERT_OK(loop().RunUntilIdle());
  ASSERT_EQ(count_outstanding_open_vnode()->GetOpenCount(), kNumActiveClients);

  // Drop all the clients, leading to |Close| being invoked on "count_outstanding_open_vnode"
  // eventually.
  clients.clear();

  ASSERT_OK(loop().RunUntilIdle());
  ASSERT_EQ(count_outstanding_open_vnode()->GetOpenCount(), 0);
}

TEST_F(ConnectionClosingTest, ClosingNodeLeadsToClosingServerEndChannel) {
  // Create connection to vfs.
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  zx_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_STATUS(ZX_ERR_TIMED_OUT,
                client_end.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), &observed));
  ASSERT_FALSE(observed & ZX_CHANNEL_PEER_CLOSED);

  ASSERT_OK(loop().StartThread());
  auto result = fidl::WireCall<fio::Node>(zx::unowned_channel(client_end)).Close();
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);

  observed = ZX_SIGNAL_NONE;
  ASSERT_OK(client_end.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
  ASSERT_TRUE(observed & ZX_CHANNEL_PEER_CLOSED);

  loop().Shutdown();
}

}  // namespace
