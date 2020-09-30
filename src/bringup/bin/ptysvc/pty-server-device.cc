// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-server-device.h"

#include "pty-client.h"

// The pty server half only supports OpenClient and SetWindowSize. Return ZX_ERR_NOT_SUPPORTED for
// all of the others

void PtyServerDevice::SetWindowSize(::llcpp::fuchsia::hardware::pty::WindowSize size,
                                    SetWindowSizeCompleter::Sync& completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::SetWindowSizeResponse> buf;
  server_->set_window_size({.width = size.width, .height = size.height});
  completer.Reply(buf.view(), ZX_OK);
}
void PtyServerDevice::OpenClient(uint32_t id, zx::channel client,
                                 OpenClientCompleter::Sync& completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::OpenClientResponse> buf;
  completer.Reply(buf.view(), server_->CreateClient(id, std::move(client)));
}

void PtyServerDevice::ClrSetFeature(uint32_t clr, uint32_t set,
                                    ClrSetFeatureCompleter::Sync& completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ClrSetFeatureResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

void PtyServerDevice::GetWindowSize(GetWindowSizeCompleter::Sync& completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::GetWindowSizeResponse> buf;
  ::llcpp::fuchsia::hardware::pty::WindowSize wsz = {.width = 0, .height = 0};
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, wsz);
}

void PtyServerDevice::MakeActive(uint32_t client_pty_id, MakeActiveCompleter::Sync& completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::MakeActiveResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

void PtyServerDevice::ReadEvents(ReadEventsCompleter::Sync& completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ReadEventsResponse> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

// Assert in all of these, since these should be handled by fs::Connection before our
// HandleFsSpecificMessage() is called.
void PtyServerDevice::Read(uint64_t count, ReadCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Clone(uint32_t flags, zx::channel node, CloneCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Close(CloseCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::Describe(DescribeCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::GetAttr(GetAttrCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::GetFlags(GetFlagsCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
                              WriteAtCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Seek(int64_t offset, ::llcpp::fuchsia::io::SeekOrigin start,
                           SeekCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Truncate(uint64_t length, TruncateCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::SetFlags(uint32_t flags, SetFlagsCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::GetBuffer(uint32_t flags, GetBufferCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Sync(SyncCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::SetAttr(uint32_t flags, ::llcpp::fuchsia::io::NodeAttributes attributes,
                              SetAttrCompleter::Sync& completer) {
  ZX_ASSERT(false);
}
