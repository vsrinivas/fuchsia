// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
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
zx::result<fio::wire::NodeInfoDeprecated> GetOnOpenResponse(
    fidl::UnownedClientEnd<fio::Node> channel) {
  zx::result<fio::wire::NodeInfoDeprecated> node_info{};
  auto get_on_open_response = [](fidl::UnownedClientEnd<fio::Node> channel,
                                 zx::result<fio::wire::NodeInfoDeprecated>& node_info) {
    class EventHandler final : public fidl::testing::WireSyncEventHandlerTestBase<fio::Node> {
     public:
      explicit EventHandler() = default;

      void OnOpen(fidl::WireEvent<fio::Node::OnOpen>* event) override {
        ASSERT_NE(event, nullptr);
        response_ = std::move(*event);
      }

      void NotImplemented_(const std::string& name) override {
        ADD_FAILURE("Unexpected %s", name.c_str());
      }

      fidl::WireEvent<fio::Node::OnOpen> GetResponse() { return std::move(response_); }

     private:
      fidl::WireEvent<fio::Node::OnOpen> response_;
    };

    EventHandler event_handler{};
    fidl::Status event_result = event_handler.HandleOneEvent(channel);
    // Expect that |on_open| was received
    ASSERT_TRUE(event_result.ok());
    fidl::WireEvent<fio::Node::OnOpen> response = event_handler.GetResponse();
    if (response.s != ZX_OK) {
      node_info = zx::error(response.s);
      return;
    }
    ASSERT_TRUE(response.info.has_value());
    node_info = zx::ok(std::move(response.info.value()));
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

  zx_status_t ConnectClient(fidl::ServerEnd<fio::Directory> server_end) {
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
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Connect to File
  zx::result fc = fidl::CreateEndpoints<fio::File>();
  ASSERT_OK(fc.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable),
                         fc->server.TakeChannel().release()));

  // Use GetFlags to get current flags and rights
  auto file_get_result = fidl::WireCall(fc->client)->GetFlags();
  EXPECT_OK(file_get_result.status());
  EXPECT_EQ(fio::wire::OpenFlags::kRightReadable, file_get_result->flags);
  {
    // Make modifications to flags with SetFlags: Note this only works for kOpenFlagAppend
    // based on posix standard
    auto file_set_result = fidl::WireCall(fc->client)->SetFlags(fio::wire::OpenFlags::kAppend);
    EXPECT_OK(file_set_result->s);
  }
  {
    // Check that the new flag is saved
    auto file_get_result = fidl::WireCall(fc->client)->GetFlags();
    EXPECT_OK(file_get_result->s);
    EXPECT_EQ(fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kAppend,
              file_get_result->flags);
  }
}

TEST_F(ConnectionTest, NodeGetSetFlagsOnDirectory) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Connect to Directory
  zx::result dc = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(dc.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "dir",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable |
                                               fio::wire::OpenFlags::kRightWritable),
                         dc->server.TakeChannel().release()));

  // Read/write/read directory flags; same as for file
  auto dir_get_result = fidl::WireCall(dc->client)->GetFlags();
  EXPECT_OK(dir_get_result->s);
  EXPECT_EQ(fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable,
            dir_get_result->flags);

  auto dir_set_result = fidl::WireCall(dc->client)->SetFlags(fio::wire::OpenFlags::kAppend);
  EXPECT_OK(dir_set_result->s);

  auto dir_get_result_2 = fidl::WireCall(dc->client)->GetFlags();
  EXPECT_OK(dir_get_result_2->s);
  EXPECT_EQ(fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable |
                fio::wire::OpenFlags::kAppend,
            dir_get_result_2->flags);
}

TEST_F(ConnectionTest, PosixFlagDirectoryRightExpansion) {
  // Create connection to VFS with all rights.
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Combinations of POSIX flags to be tested.
  const fio::wire::OpenFlags OPEN_FLAG_COMBINATIONS[]{
      fio::wire::OpenFlags::kPosixWritable, fio::wire::OpenFlags::kPosixExecutable,
      fio::wire::OpenFlags::kPosixWritable | fio::wire::OpenFlags::kPosixExecutable};

  for (const fio::wire::OpenFlags OPEN_FLAGS : OPEN_FLAG_COMBINATIONS) {
    // Connect to drectory specifying the flag combination we want to test.
    zx::result dc = fidl::CreateEndpoints<fio::Directory>();
    ASSERT_OK(dc.status_value());
    ASSERT_OK(fdio_open_at(root->client.channel().get(), "dir",
                           static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable | OPEN_FLAGS),
                           dc->server.TakeChannel().release()));

    // Ensure flags match those which we expect.
    auto dir_get_result = fidl::WireCall(dc->client)->GetFlags();
    EXPECT_OK(dir_get_result->s);
    auto dir_flags = dir_get_result->flags;
    EXPECT_TRUE(fio::wire::OpenFlags::kRightReadable & dir_flags);
    // Each POSIX flag should be expanded to its respective right(s).
    if (OPEN_FLAGS & fio::wire::OpenFlags::kPosixWritable)
      EXPECT_TRUE(fio::wire::OpenFlags::kRightWritable & dir_flags);
    if (OPEN_FLAGS & fio::wire::OpenFlags::kPosixExecutable)
      EXPECT_TRUE(fio::wire::OpenFlags::kRightExecutable & dir_flags);

    // Repeat test, but for file, which should not have any expanded rights.
    zx::result fc = fidl::CreateEndpoints<fio::File>();
    ASSERT_OK(fc.status_value());
    ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                           static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable | OPEN_FLAGS),
                           fc->server.TakeChannel().release()));
    auto file_get_result = fidl::WireCall(fc->client)->GetFlags();
    EXPECT_OK(file_get_result.status());
    EXPECT_EQ(fio::wire::OpenFlags::kRightReadable, file_get_result->flags);
  }
}

TEST_F(ConnectionTest, FileGetSetFlagsOnFile) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Connect to File
  zx::result fc = fidl::CreateEndpoints<fio::File>();
  ASSERT_OK(fc.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable),
                         fc->server.TakeChannel().release()));

  {
    // Use GetFlags to get current flags and rights
    auto file_get_result = fidl::WireCall(fc->client)->GetFlags();
    EXPECT_OK(file_get_result.status());
    EXPECT_EQ(fio::wire::OpenFlags::kRightReadable, file_get_result->flags);
  }
  {
    // Make modifications to flags with SetFlags: Note this only works for kOpenFlagAppend
    // based on posix standard
    auto file_set_result = fidl::WireCall(fc->client)->SetFlags(fio::wire::OpenFlags::kAppend);
    EXPECT_OK(file_set_result->s);
  }
  {
    // Check that the new flag is saved
    auto file_get_result = fidl::WireCall(fc->client)->GetFlags();
    EXPECT_OK(file_get_result->s);
    EXPECT_EQ(fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kAppend,
              file_get_result->flags);
  }
}

TEST_F(ConnectionTest, FileSeekDirectory) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Interacting with a Directory connection using File protocol methods should fail.
  {
    zx::result dc = fidl::CreateEndpoints<fio::Directory>();
    ASSERT_OK(dc.status_value());
    ASSERT_OK(fdio_open_at(root->client.channel().get(), "dir",
                           static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable |
                                                 fio::wire::OpenFlags::kRightWritable),
                           dc->server.TakeChannel().release()));

    // Borrowing directory channel as file channel.
    auto dir_get_result =
        fidl::WireCall(fidl::UnownedClientEnd<fio::File>(dc->client.borrow().handle()))
            ->Seek(fio::wire::SeekOrigin::kStart, 0);
    EXPECT_NOT_OK(dir_get_result.status());
  }
}

TEST_F(ConnectionTest, NegotiateProtocol) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  constexpr uint32_t kOpenMode = 0755;

  // Connect to polymorphic node as a directory, by passing |kOpenFlagDirectory|.
  zx::result dc = fidl::CreateEndpoints<fio::Node>();
  ASSERT_OK(dc.status_value());
  ASSERT_OK(fidl::WireCall(root->client)
                ->Open(fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kDescribe |
                           fio::wire::OpenFlags::kDirectory,
                       kOpenMode, fidl::StringView("file_or_dir"), std::move(dc->server))
                .status());
  zx::result<fio::wire::NodeInfoDeprecated> dir_info = GetOnOpenResponse(dc->client);
  ASSERT_OK(dir_info);
  ASSERT_TRUE(dir_info->is_directory());

  // Connect to polymorphic node as a file, by passing |kOpenFlagNotDirectory|.
  zx::result fc = fidl::CreateEndpoints<fio::Node>();
  ASSERT_OK(fc.status_value());
  ASSERT_OK(fidl::WireCall(root->client)
                ->Open(fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kDescribe |
                           fio::wire::OpenFlags::kNotDirectory,
                       kOpenMode, fidl::StringView("file_or_dir"), std::move(fc->server))
                .status());
  zx::result<fio::wire::NodeInfoDeprecated> file_info = GetOnOpenResponse(fc->client);
  ASSERT_OK(file_info);
  ASSERT_TRUE(file_info->is_file());
}

TEST_F(ConnectionTest, PrevalidateFlagsOpenFailure) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  constexpr uint32_t kOpenMode = 0755;
  // Flag combination which should return INVALID_ARGS (see PrevalidateFlags in connection.cc).
  constexpr fio::wire::OpenFlags kInvalidFlagCombo =
      fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kDescribe |
      fio::wire::OpenFlags::kDirectory | fio::wire::OpenFlags::kNodeReference |
      fio::wire::OpenFlags::kAppend;
  zx::result dc = fidl::CreateEndpoints<fio::Node>();
  ASSERT_OK(dc.status_value());
  // Ensure that invalid flag combination returns INVALID_ARGS.
  ASSERT_OK(fidl::WireCall(root->client)
                ->Open(kInvalidFlagCombo, kOpenMode, fidl::StringView("file_or_dir"),
                       std::move(dc->server))
                .status());
  ASSERT_EQ(GetOnOpenResponse(dc->client).status_value(), ZX_ERR_INVALID_ARGS);
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

  zx_status_t ConnectClient(fidl::ServerEnd<fuchsia_io::Directory> server_end) {
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
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  constexpr uint32_t kOpenMode = 0755;
  constexpr int kNumActiveClients = 20;

  ASSERT_EQ(count_outstanding_open_vnode()->GetOpenCount(), 0);

  // Create a number of active connections to "count_outstanding_open_vnode".
  std::vector<fidl::ClientEnd<fio::Node>> clients;
  for (int i = 0; i < kNumActiveClients; i++) {
    zx::result fc = fidl::CreateEndpoints<fio::Node>();
    ASSERT_OK(fc.status_value());
    ASSERT_OK(fidl::WireCall(root->client)
                  ->Open(fio::wire::OpenFlags::kRightReadable, kOpenMode,
                         fidl::StringView("count_outstanding_open_vnode"), std::move(fc->server))
                  .status());
    clients.push_back(std::move(fc->client));
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
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  zx_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_STATUS(ZX_ERR_TIMED_OUT,
                root->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(),
                                                &observed));
  ASSERT_FALSE(observed & ZX_CHANNEL_PEER_CLOSED);

  ASSERT_OK(loop().StartThread());
  auto result = fidl::WireCall(root->client)->Close();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok(), "%s", zx_status_get_string(result->error_value()));

  observed = ZX_SIGNAL_NONE;
  ASSERT_OK(
      root->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
  ASSERT_TRUE(observed & ZX_CHANNEL_PEER_CLOSED);

  loop().Shutdown();
}

}  // namespace
