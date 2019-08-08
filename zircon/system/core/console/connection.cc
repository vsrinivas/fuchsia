// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <thread>

#include <fbl/algorithm.h>

namespace {

class Transaction : public fidl::Transaction {
 public:
  Transaction(fidl_txn_t* txn) : txn_(txn) {}

  ~Transaction() {
    ZX_ASSERT_MSG(status_called_,
                  "Transaction must have it's Status() method used.  This provides "
                  "HandleFsSpecificMessage with the correct status value.\n");
  }

  /// Status() return the internal state of the DDK transaction. This MUST be called
  /// to bridge the Transaction and DDK dispatcher.
  zx_status_t Status() __WARN_UNUSED_RESULT {
    status_called_ = true;
    return status_;
  }

 protected:
  void Reply(fidl::Message msg) final {
    ZX_ASSERT(txn_);

    const fidl_msg_t fidl_msg{
        .bytes = msg.bytes().data(),
        .handles = msg.handles().data(),
        .num_bytes = static_cast<uint32_t>(msg.bytes().size()),
        .num_handles = static_cast<uint32_t>(msg.handles().size()),
    };

    status_ = txn_->reply(txn_, &fidl_msg);
  }

  void Close(zx_status_t close_status) final { status_ = close_status; }

  std::unique_ptr<fidl::Transaction> TakeOwnership() final {
    ZX_ASSERT_MSG(false, "DdkTransaction cannot take ownership of the transaction.\n");
  }

 private:
  fidl_txn_t* txn_;
  zx_status_t status_ = ZX_OK;
  bool status_called_ = false;
};

}  // namespace

zx_status_t Connection::HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  Transaction transaction{txn};
  if (!::llcpp::fuchsia::hardware::pty::Device::TryDispatch(this, msg, &transaction)) {
    __UNUSED auto ignore = transaction.Status();
    return ZX_ERR_NOT_SUPPORTED;
  }
  return transaction.Status();
}

// Return ZX_ERR_NOT_SUPPORTED for all of the PTY things we don't actually support
void Connection::OpenClient(uint32_t id, zx::channel client, OpenClientCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::OpenClientResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

void Connection::ClrSetFeature(uint32_t clr, uint32_t set, ClrSetFeatureCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ClrSetFeatureResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

void Connection::GetWindowSize(GetWindowSizeCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::GetWindowSizeResponse> buf;
  ::llcpp::fuchsia::hardware::pty::WindowSize wsz = {.width = 0, .height = 0};
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, wsz);
}

void Connection::MakeActive(uint32_t client_pty_id, MakeActiveCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::MakeActiveResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

void Connection::ReadEvents(ReadEventsCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ReadEventsResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

void Connection::SetWindowSize(::llcpp::fuchsia::hardware::pty::WindowSize size,
                               SetWindowSizeCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::SetWindowSizeResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

// Assert in all of these, since these should be handled by fs::Connection before our
// HandleFsSpecificMessage() is called.
void Connection::Read(uint64_t count, ReadCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void Connection::Clone(uint32_t flags, zx::channel node, CloneCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void Connection::Close(CloseCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::Describe(DescribeCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::GetAttr(GetAttrCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::GetFlags(GetFlagsCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void Connection::WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
                         WriteAtCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void Connection::Seek(int64_t offset, ::llcpp::fuchsia::io::SeekOrigin start,
                      SeekCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void Connection::Truncate(uint64_t length, TruncateCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::SetFlags(uint32_t flags, SetFlagsCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::Sync(SyncCompleter::Sync completer) { ZX_ASSERT(false); }

void Connection::SetAttr(uint32_t flags, ::llcpp::fuchsia::io::NodeAttributes attributes,
                         SetAttrCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void Connection::Ioctl(uint32_t opcode, uint64_t max_out, fidl::VectorView<zx::handle> handles,
                       fidl::VectorView<uint8_t> in, IoctlCompleter::Sync completer) {
  ZX_ASSERT(false);
}
