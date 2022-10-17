// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/wire/message.h>
#include <stdio.h>
#include <zircon/fidl.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {

namespace fio = fuchsia_io;

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
  void HandleFsSpecificMessage(fidl::IncomingHeaderAndMessage& msg,
                               fidl::Transaction* txn) override {
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
    root_ = fbl::MakeRefCounted<fs::PseudoDir>();
    file_ = fbl::MakeRefCounted<TestVnode>();
    root_->AddEntry("file", file_);
  }

  zx_status_t ConnectClient(fidl::ServerEnd<fio::Directory> server_end) {
    // Serve root directory with maximum rights
    return vfs_.ServeDirectory(root_, std::move(server_end));
  }

  std::unique_ptr<fidl::Transaction> GetNextInflightTransaction() {
    return file_->GetNextInflightTransaction();
  }

  size_t inflight_transactions() { return file_->GetInflightTransactions(); }

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
  fidl::InitTxnHeader(&hdr, 1, 1, fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(c.write(0, &hdr, sizeof(hdr), nullptr, 0));
}

template <typename Protocol>
void SendHangingMessage(const fidl::ClientEnd<Protocol>& client_end) {
  SendHangingMessage(client_end.channel());
}

TEST_F(TransactionCountingTest, CountStartsAtZero) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  ASSERT_EQ(inflight_transactions(), 0);

  // Connect to file
  zx::result fc = fidl::CreateEndpoints<fio::File>();
  ASSERT_OK(fc.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable),
                         fc->server.TakeChannel().release()));

  ASSERT_EQ(inflight_transactions(), 0);
}

TEST_F(TransactionCountingTest, SingleTransactionInflightReplyShortMessage) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Connect to file
  zx::result fc = fidl::CreateEndpoints<fio::File>();
  ASSERT_OK(fc.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable),
                         fc->server.TakeChannel().release()));

  SendHangingMessage(fc->client);
  {
    auto txn = GetNextInflightTransaction();
    ASSERT_EQ(inflight_transactions(), 1);
    fidl_message_header_t header;
    zx_channel_iovec_t iovecs[1] = {
        {
            .buffer = &header,
            .capacity = sizeof(header),
            .reserved = 0,
        },
    };
    fidl_outgoing_msg_t c_msg = {
        .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
        .iovec =
            {
                .iovecs = iovecs,
                .num_iovecs = std::size(iovecs),
            },
    };
    auto message = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);

    txn->Reply(&message);
    // Count drops when the transaction object is destroyed
    ASSERT_EQ(inflight_transactions(), 1);
  }
  ASSERT_EQ(inflight_transactions(), 0);
}

TEST_F(TransactionCountingTest, SingleTransactionInflightReplyValidMessage) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Connect to file
  zx::result fc = fidl::CreateEndpoints<fio::File>();
  ASSERT_OK(fc.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable),
                         fc->server.TakeChannel().release()));

  SendHangingMessage(fc->client);
  {
    auto txn = GetNextInflightTransaction();
    ASSERT_EQ(inflight_transactions(), 1);

    fidl_message_header_t hdr = {};
    fidl::InitTxnHeader(&hdr, 1, 1, fidl::MessageDynamicFlags::kStrictMethod);

    zx_channel_iovec_t iovecs[1] = {
        {
            .buffer = &hdr,
            .capacity = sizeof(hdr),
            .reserved = 0,
        },
    };
    fidl_outgoing_msg_t c_msg = {
        .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
        .iovec =
            {
                .iovecs = iovecs,
                .num_iovecs = std::size(iovecs),
            },
    };
    auto message = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);

    txn->Reply(&message);
    // Count drops when the transaction object is destroyed
    ASSERT_EQ(inflight_transactions(), 1);
  }
  ASSERT_EQ(inflight_transactions(), 0);
}

TEST_F(TransactionCountingTest, SingleTransactionInflightCloseOnMessage) {
  // Create connection to vfs
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Connect to file
  zx::result fc = fidl::CreateEndpoints<fio::File>();
  ASSERT_OK(fc.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable),
                         fc->server.TakeChannel().release()));

  SendHangingMessage(fc->client);
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
  zx::result root = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(root.status_value());
  ASSERT_OK(ConnectClient(std::move(root->server)));

  // Connect to file twice
  zx::result fc1 = fidl::CreateEndpoints<fio::File>();
  ASSERT_OK(fc1.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable),
                         fc1->server.TakeChannel().release()));

  zx::result fc2 = fidl::CreateEndpoints<fio::File>();
  ASSERT_OK(fc2.status_value());
  ASSERT_OK(fdio_open_at(root->client.channel().get(), "file",
                         static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable),
                         fc2->server.TakeChannel().release()));

  SendHangingMessage(fc1->client);
  auto txn1 = GetNextInflightTransaction();
  SendHangingMessage(fc2->client);
  auto txn2 = GetNextInflightTransaction();

  ASSERT_EQ(inflight_transactions(), 2);

  fidl_message_header_t header;
  {
    zx_channel_iovec_t iovecs[1] = {
        {
            .buffer = &header,
            .capacity = sizeof(header),
            .reserved = 0,
        },
    };
    fidl_outgoing_msg_t c_msg = {
        .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
        .iovec =
            {
                .iovecs = iovecs,
                .num_iovecs = std::size(iovecs),
            },
    };
    auto message = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
    txn1->Reply(&message);
    txn1.reset();
  }
  ASSERT_EQ(inflight_transactions(), 1);

  {
    zx_channel_iovec_t iovecs[1] = {
        {
            .buffer = &header,
            .capacity = sizeof(header),
            .reserved = 0,
        },
    };
    fidl_outgoing_msg_t c_msg = {
        .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
        .iovec =
            {
                .iovecs = iovecs,
                .num_iovecs = std::size(iovecs),
            },
    };
    auto message = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
    txn2->Reply(&message);
    txn2.reset();
  }
  ASSERT_EQ(inflight_transactions(), 0);
}

}  // namespace
