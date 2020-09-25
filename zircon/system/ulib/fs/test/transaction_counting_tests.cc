// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/llcpp/message.h>
#include <stdio.h>
#include <zircon/fidl.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

#include <fs/pseudo_dir.h>
#include <fs/pseudo_file.h>
#include <fs/synchronous_vfs.h>
#include <zxtest/zxtest.h>

namespace {

namespace fio = ::llcpp::fuchsia::io;

// Vnode that gives us control of when it replies to messages
class TestVnode : public fs::Vnode {
 public:
  TestVnode() = default;

  fs::VnodeProtocolSet GetProtocols() const override { return fs::VnodeProtocol::kFile; }

  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights,
                                     fs::VnodeRepresentation* info) override {
    ZX_ASSERT(protocol == fs::VnodeProtocol::kFile);
    *info = fs::VnodeRepresentation::File();
    return ZX_OK;
  }

  // The test code below sends a message unrecognized by fs::Vnode, and we use that to make this
  // transaction asynchronous and enqueue the transaction to be completed when desired by the test
  // logic.
  void HandleFsSpecificMessage(fidl_msg_t* msg, fidl::Transaction* txn) override {
    std::unique_lock<std::mutex> guard(transactions_lock_);
    transactions_.push_back(txn->TakeOwnership());
    transactions_cv_.notify_all();
  }

  // Blocks until the the FIDL message has been dispatched to HandleFsSpecificMessage, and the
  // transaction is available
  std::unique_ptr<fidl::Transaction> GetNextInflightTransaction() {
    std::unique_lock<std::mutex> guard(transactions_lock_);
    transactions_cv_.wait(guard, [this]() { return !transactions_.empty(); });

    auto result = std::move(transactions_.back());
    transactions_.pop_back();
    return result;
  }

 private:
  std::mutex transactions_lock_;
  std::condition_variable transactions_cv_;
  std::vector<std::unique_ptr<fidl::Transaction>> transactions_;
};

class TransactionCountingTest : public zxtest::Test {
 public:
  // Setup file structure with one directory and one file. Note: On creation
  // directories and files have no flags and rights.
  TransactionCountingTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_.SetDispatcher(loop_.dispatcher());
    root_ = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    file_ = fbl::AdoptRef<TestVnode>(new TestVnode());
    root_->AddEntry("file", file_);
  }

  zx_status_t ConnectClient(zx::channel server_end) {
    // Serve root directory with maximum rights
    return vfs_.ServeDirectory(root_, std::move(server_end));
  }

  std::unique_ptr<fidl::Transaction> GetNextInflightTransaction() {
    return file_->GetNextInflightTransaction();
  }

  size_t inflight_transactions() { return file_->inflight_transactions(); }

 protected:
  void SetUp() override { loop_.StartThread(); }

  void TearDown() override { loop_.Shutdown(); }

 private:
  async::Loop loop_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;
  fbl::RefPtr<TestVnode> file_;
};

void SendHangingMessage(const zx::channel& c) {
  fidl_message_header_t hdr = {};
  fidl_init_txn_header(&hdr, 1, 1);
  ASSERT_OK(c.write(0, &hdr, sizeof(hdr), nullptr, 0));
}

TEST_F(TransactionCountingTest, CountStartsAtZero) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  ASSERT_EQ(inflight_transactions(), 0);

  // Connect to file
  zx::channel fc1, fc1_remote;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc1_remote));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc1_remote.release()));

  ASSERT_EQ(inflight_transactions(), 0);
}

TEST_F(TransactionCountingTest, SingleTransactionInflightReplyShortMessage) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to file
  zx::channel fc1, fc1_remote;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc1_remote));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc1_remote.release()));

  SendHangingMessage(fc1);
  {
    auto txn = GetNextInflightTransaction();
    ASSERT_EQ(inflight_transactions(), 1);
    fidl_message_header_t header;
    fidl::FidlMessage message(reinterpret_cast<uint8_t*>(&header), sizeof(header), sizeof(header),
                              nullptr, 0, 0);
    txn->Reply(&message);
    // Count drops when the transaction object is destroyed
    ASSERT_EQ(inflight_transactions(), 1);
  }
  ASSERT_EQ(inflight_transactions(), 0);
}

TEST_F(TransactionCountingTest, SingleTransactionInflightReplyValidMessage) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to file
  zx::channel fc1, fc1_remote;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc1_remote));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc1_remote.release()));

  SendHangingMessage(fc1);
  {
    auto txn = GetNextInflightTransaction();
    ASSERT_EQ(inflight_transactions(), 1);

    fidl_message_header_t hdr = {};
    fidl_init_txn_header(&hdr, 1, 1);

    fidl::FidlMessage message(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr), sizeof(hdr), nullptr,
                              0, 0);
    txn->Reply(&message);
    // Count drops when the transaction object is destroyed
    ASSERT_EQ(inflight_transactions(), 1);
  }
  ASSERT_EQ(inflight_transactions(), 0);
}

TEST_F(TransactionCountingTest, SingleTransactionInflightCloseOnMessage) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to file
  zx::channel fc1, fc1_remote;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc1_remote));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc1_remote.release()));

  SendHangingMessage(fc1);
  {
    auto txn = GetNextInflightTransaction();
    ASSERT_EQ(inflight_transactions(), 1);

    txn->Close(ZX_OK);
    // Count drops when the transaction object is destroyed
    ASSERT_EQ(inflight_transactions(), 1);
  }
  ASSERT_EQ(inflight_transactions(), 0);
}

TEST_F(TransactionCountingTest, MultipleTransactionsInflight) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to file twice
  zx::channel fc1, fc1_remote;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc1_remote));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc1_remote.release()));

  zx::channel fc2, fc2_remote;
  ASSERT_OK(zx::channel::create(0u, &fc2, &fc2_remote));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc2_remote.release()));

  SendHangingMessage(fc1);
  auto txn1 = GetNextInflightTransaction();
  SendHangingMessage(fc2);
  auto txn2 = GetNextInflightTransaction();

  ASSERT_EQ(inflight_transactions(), 2);

  fidl_message_header_t header;
  {
    fidl::FidlMessage message(reinterpret_cast<uint8_t*>(&header), sizeof(header), sizeof(header),
                              nullptr, 0, 0);
    txn1->Reply(&message);
    txn1.reset();
  }
  ASSERT_EQ(inflight_transactions(), 1);

  {
    fidl::FidlMessage message(reinterpret_cast<uint8_t*>(&header), sizeof(header), sizeof(header),
                              nullptr, 0, 0);
    txn2->Reply(&message);
    txn2.reset();
  }
  ASSERT_EQ(inflight_transactions(), 0);
}

}  // namespace
