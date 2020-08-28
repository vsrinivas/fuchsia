// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>

#include <atomic>
#include <utility>

#include <fs/pseudo_dir.h>
#include <fs/pseudo_file.h>
#include <fs/synchronous_vfs.h>
#include <zxtest/zxtest.h>

namespace {

namespace fio = ::llcpp::fuchsia::io;

zx_status_t DummyReader(fbl::String* output) { return ZX_OK; }

zx_status_t DummyWriter(fbl::StringPiece input) { return ZX_OK; }

// Example vnode that supports protocol negotiation.
// Here the vnode may be opened as a file or a directory.
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

class VfsTestSetup : public zxtest::Test {
 public:
  // Setup file structure with one directory and one file. Note: On creation
  // directories and files have no flags and rights.
  VfsTestSetup() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_.SetDispatcher(loop_.dispatcher());
    root_ = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    dir_ = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    file_ = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(&DummyReader, &DummyWriter));
    file_or_dir_ = fbl::AdoptRef<FileOrDirectory>(new FileOrDirectory());
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
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc2.release()));

  // Use NodeGetFlags to get current flags and rights
  auto file_get_result = fio::Node::Call::NodeGetFlags(zx::unowned_channel(fc1));
  EXPECT_OK(file_get_result.status());
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE, file_get_result.Unwrap()->flags);
  {
    // Make modifications to flags with NodeSetFlags: Note this only works for OPEN_FLAG_APPEND
    // based on posix standard
    auto file_set_result =
        fio::Node::Call::NodeSetFlags(zx::unowned_channel(fc1), fio::OPEN_FLAG_APPEND);
    EXPECT_OK(file_set_result.Unwrap()->s);
  }
  {
    // Check that the new flag is saved
    auto file_get_result = fio::Node::Call::NodeGetFlags(zx::unowned_channel(fc1));
    EXPECT_OK(file_get_result.Unwrap()->s);
    EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_APPEND, file_get_result.Unwrap()->flags);
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
                         fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, dc2.release()));

  // Read/write/read directory flags; same as for file
  auto dir_get_result = fio::Node::Call::NodeGetFlags(zx::unowned_channel(dc1));
  EXPECT_OK(dir_get_result.Unwrap()->s);
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, dir_get_result.Unwrap()->flags);

  auto dir_set_result =
      fio::Node::Call::NodeSetFlags(zx::unowned_channel(dc1), fio::OPEN_FLAG_APPEND);
  EXPECT_OK(dir_set_result.Unwrap()->s);

  auto dir_get_result_2 = fio::Node::Call::NodeGetFlags(zx::unowned_channel(dc1));
  EXPECT_OK(dir_get_result_2.Unwrap()->s);
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_APPEND,
            dir_get_result_2.Unwrap()->flags);
}

TEST_F(ConnectionTest, FileGetSetFlagsOnFile) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to File
  zx::channel fc1, fc2;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc2.release()));

  {
    // Use NodeGetFlags to get current flags and rights
    auto file_get_result = fio::File::Call::GetFlags(zx::unowned_channel(fc1));
    EXPECT_OK(file_get_result.status());
    EXPECT_EQ(fio::OPEN_RIGHT_READABLE, file_get_result.Unwrap()->flags);
  }
  {
    // Make modifications to flags with NodeSetFlags: Note this only works for OPEN_FLAG_APPEND
    // based on posix standard
    auto file_set_result =
        fio::File::Call::SetFlags(zx::unowned_channel(fc1), fio::OPEN_FLAG_APPEND);
    EXPECT_OK(file_set_result.Unwrap()->s);
  }
  {
    // Check that the new flag is saved
    auto file_get_result = fio::File::Call::GetFlags(zx::unowned_channel(fc1));
    EXPECT_OK(file_get_result.Unwrap()->s);
    EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_APPEND, file_get_result.Unwrap()->flags);
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
                           fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, dc2.release()));

    auto dir_get_result = fio::File::Call::GetFlags(zx::unowned_channel(dc1));
    EXPECT_NOT_OK(dir_get_result.status());
  }

  {
    zx::channel dc1, dc2;
    ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
    ASSERT_OK(fdio_open_at(client_end.get(), "dir",
                           fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, dc2.release()));

    auto dir_set_result =
        fio::File::Call::SetFlags(zx::unowned_channel(dc1), fio::OPEN_FLAG_APPEND);
    EXPECT_NOT_OK(dir_set_result.status());
  }
}

TEST_F(ConnectionTest, NegotiateProtocol) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Helper method to monitor the OnOpen event, used by the tests below
  auto expect_on_open = [](zx::unowned_channel channel, fit::function<void(fio::NodeInfo)> cb) {
    fio::Node::EventHandlers handlers{.on_open =
                                          [&](fio::Node::OnOpenResponse* message) {
                                            EXPECT_OK(message->s);
                                            EXPECT_FALSE(message->info.has_invalid_tag());
                                            cb(std::move(message->info));
                                            return ZX_OK;
                                          },
                                      .unknown = []() { return ZX_ERR_INVALID_ARGS; }};
    fidl::Result event_result = fio::Node::Call::HandleEvents(std::move(channel), handlers);
    // Expect that |on_open| was received
    EXPECT_TRUE(event_result.ok());
  };

  constexpr uint32_t kOpenMode = 0755;

  // Connect to polymorphic node as a directory, by passing |OPEN_FLAG_DIRECTORY|.
  zx::channel dc1, dc2;
  ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
  ASSERT_OK(fio::Directory::Call::Open(
                zx::unowned_channel(client_end),
                fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_DESCRIBE | fio::OPEN_FLAG_DIRECTORY,
                kOpenMode, fidl::StringView("file_or_dir"), std::move(dc2))
                .status());
  expect_on_open(zx::unowned_channel(dc1),
                 [](fio::NodeInfo info) { EXPECT_TRUE(info.is_directory()); });

  // Connect to polymorphic node as a file, by passing |OPEN_FLAG_NOT_DIRECTORY|.
  zx::channel fc1, fc2;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
  ASSERT_OK(fio::Directory::Call::Open(
                zx::unowned_channel(client_end),
                fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_DESCRIBE | fio::OPEN_FLAG_NOT_DIRECTORY,
                kOpenMode, fidl::StringView("file_or_dir"), std::move(fc2))
                .status());
  expect_on_open(zx::unowned_channel(fc1), [](fio::NodeInfo info) { EXPECT_TRUE(info.is_file()); });
}

// A vnode which maintains a counter of number of |Open| calls that
// have not been balanced out with a |Close|.
class CountOutstandingOpenVnode : public fs::Vnode {
 public:
  CountOutstandingOpenVnode() = default;

  fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kFile; }

  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final {
    *info = fs::VnodeRepresentation::File{};
    return ZX_OK;
  }

  zx_status_t Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) final {
    num_open_.fetch_add(1);
    return ZX_OK;
  }

  zx_status_t Close() final {
    num_open_.fetch_sub(1);
    return ZX_OK;
  }

  uint64_t num_open() const { return num_open_.load(); }

 private:
  std::atomic<uint64_t> num_open_ = {};
};

class ConnectionClosingTest : public zxtest::Test {
 public:
  // Setup file structure with one directory and one file. Note: On creation
  // directories and files have no flags and rights.
  ConnectionClosingTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_.SetDispatcher(loop_.dispatcher());
    root_ = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    count_outstanding_open_vnode_ = fbl::AdoptRef(new CountOutstandingOpenVnode());
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

  ASSERT_EQ(count_outstanding_open_vnode()->num_open(), 0);

  // Create a number of active connections to "count_outstanding_open_vnode".
  std::vector<zx::channel> clients;
  for (int i = 0; i < kNumActiveClients; i++) {
    zx::channel fc1, fc2;
    ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
    ASSERT_OK(fio::Directory::Call::Open(
                  zx::unowned_channel(client_end), fio::OPEN_RIGHT_READABLE, kOpenMode,
                  fidl::StringView("count_outstanding_open_vnode"), std::move(fc2))
                  .status());
    clients.push_back(std::move(fc1));
  }

  ASSERT_OK(loop().RunUntilIdle());
  ASSERT_EQ(count_outstanding_open_vnode()->num_open(), kNumActiveClients);

  // Drop all the clients, leading to |Close| being invoked
  // on "count_outstanding_open_vnode" eventually.
  clients.clear();

  ASSERT_OK(loop().RunUntilIdle());
  ASSERT_EQ(count_outstanding_open_vnode()->num_open(), 0);
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
  auto result = fio::Node::Call::Close(zx::unowned_channel(client_end));
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);

  observed = ZX_SIGNAL_NONE;
  ASSERT_OK(client_end.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
  ASSERT_TRUE(observed & ZX_CHANNEL_PEER_CLOSED);

  loop().Shutdown();
}

}  // namespace
