// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-server-connection.h"

#include "pty-client.h"
#include "pty-transaction.h"

zx_status_t PtyServerConnection::HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  PtyTransaction transaction{txn};
  if (!::llcpp::fuchsia::hardware::pty::Device::TryDispatch(this, msg, &transaction)) {
    __UNUSED auto ignore = transaction.Status();
    return ZX_ERR_NOT_SUPPORTED;
  }
  return transaction.Status();
}

// The pty server half only supports OpenClient and SetWindowSize. Return ZX_ERR_NOT_SUPPORTED for
// all of the others

void PtyServerConnection::SetWindowSize(::llcpp::fuchsia::hardware::pty::WindowSize size,
                                        SetWindowSizeCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::SetWindowSizeResponse> buf;
  server_->set_window_size({.width = size.width, .height = size.height});
  completer.Reply(buf.view(), ZX_OK);
}
void PtyServerConnection::OpenClient(uint32_t id, zx::channel client,
                                     OpenClientCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::OpenClientResponse> buf;
  completer.Reply(buf.view(), server_->CreateClient(id, std::move(client)));
}

void PtyServerConnection::ClrSetFeature(uint32_t clr, uint32_t set,
                                        ClrSetFeatureCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ClrSetFeatureResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

void PtyServerConnection::GetWindowSize(GetWindowSizeCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::GetWindowSizeResponse> buf;
  ::llcpp::fuchsia::hardware::pty::WindowSize wsz = {.width = 0, .height = 0};
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, wsz);
}

void PtyServerConnection::MakeActive(uint32_t client_pty_id, MakeActiveCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::MakeActiveResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

void PtyServerConnection::ReadEvents(ReadEventsCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ReadEventsResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

// Assert in all of these, since these should be handled by fs::Connection before our
// HandleFsSpecificMessage() is called.
void PtyServerConnection::Read(uint64_t count, ReadCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyServerConnection::Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyServerConnection::Clone(uint32_t flags, zx::channel node, CloneCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyServerConnection::Close(CloseCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyServerConnection::Describe(DescribeCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyServerConnection::GetAttr(GetAttrCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyServerConnection::GetFlags(GetFlagsCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyServerConnection::ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyServerConnection::WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
                                  WriteAtCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyServerConnection::Seek(int64_t offset, ::llcpp::fuchsia::io::SeekOrigin start,
                               SeekCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyServerConnection::Truncate(uint64_t length, TruncateCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyServerConnection::SetFlags(uint32_t flags, SetFlagsCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyServerConnection::GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyServerConnection::Sync(SyncCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyServerConnection::SetAttr(uint32_t flags, ::llcpp::fuchsia::io::NodeAttributes attributes,
                                  SetAttrCompleter::Sync completer) {
  ZX_ASSERT(false);
}
