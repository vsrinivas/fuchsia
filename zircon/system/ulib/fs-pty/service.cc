// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fs-pty/service.h>
#include <lib/fs-pty/tty-connection-internal.h>
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

namespace fs_pty::internal {

void DispatchPtyDeviceMessage(::llcpp::fuchsia::hardware::pty::Device::Interface* interface,
                              fidl_msg_t* msg, fidl::Transaction* txn) {
  ::llcpp::fuchsia::hardware::pty::Device::Dispatch(interface, msg, txn);
}

// Return ZX_ERR_NOT_SUPPORTED for all of the PTY things we don't actually support
void NullPtyDeviceImpl::OpenClient(uint32_t id, zx::channel client,
                                   OpenClientCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::OpenClientResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

void NullPtyDeviceImpl::ClrSetFeature(uint32_t clr, uint32_t set,
                                      ClrSetFeatureCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ClrSetFeatureResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

void NullPtyDeviceImpl::GetWindowSize(GetWindowSizeCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::GetWindowSizeResponse> buf;
  ::llcpp::fuchsia::hardware::pty::WindowSize wsz = {.width = 0, .height = 0};
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, wsz);
}

void NullPtyDeviceImpl::MakeActive(uint32_t client_pty_id, MakeActiveCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::MakeActiveResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

void NullPtyDeviceImpl::ReadEvents(ReadEventsCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ReadEventsResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

void NullPtyDeviceImpl::SetWindowSize(::llcpp::fuchsia::hardware::pty::WindowSize size,
                                      SetWindowSizeCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::SetWindowSizeResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

// We need to provide these methods because |fuchsia.hardware.pty.Device| composes |fuchsia.io|.
// Assert in all of these, since these should be handled by fs::Connection before our
// HandleFsSpecificMessage() is called.
void NullPtyDeviceImpl::Read(uint64_t count, ReadCompleter::Sync completer) { ZX_ASSERT(false); }

void NullPtyDeviceImpl::Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void NullPtyDeviceImpl::Clone(uint32_t flags, zx::channel node, CloneCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void NullPtyDeviceImpl::Close(CloseCompleter::Sync completer) { ZX_ASSERT(false); }

void NullPtyDeviceImpl::Describe(DescribeCompleter::Sync completer) { ZX_ASSERT(false); }

void NullPtyDeviceImpl::GetAttr(GetAttrCompleter::Sync completer) { ZX_ASSERT(false); }

void NullPtyDeviceImpl::GetFlags(GetFlagsCompleter::Sync completer) { ZX_ASSERT(false); }

void NullPtyDeviceImpl::ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void NullPtyDeviceImpl::WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
                                WriteAtCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void NullPtyDeviceImpl::Seek(int64_t offset, ::llcpp::fuchsia::io::SeekOrigin start,
                             SeekCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void NullPtyDeviceImpl::Truncate(uint64_t length, TruncateCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void NullPtyDeviceImpl::SetFlags(uint32_t flags, SetFlagsCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void NullPtyDeviceImpl::GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void NullPtyDeviceImpl::Sync(SyncCompleter::Sync completer) { ZX_ASSERT(false); }

void NullPtyDeviceImpl::SetAttr(uint32_t flags, ::llcpp::fuchsia::io::NodeAttributes attributes,
                                SetAttrCompleter::Sync completer) {
  ZX_ASSERT(false);
}

}  // namespace fs_pty::internal
