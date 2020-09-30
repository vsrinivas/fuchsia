// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-client-device.h"

#include "pty-client.h"

void PtyClientDevice::SetWindowSize(::llcpp::fuchsia::hardware::pty::WindowSize size,
                                    SetWindowSizeCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::SetWindowSizeResponse> buf;
  client_->server()->set_window_size({.width = size.width, .height = size.height});
  completer.Reply(buf.view(), ZX_OK);
}
void PtyClientDevice::OpenClient(uint32_t id, zx::channel client,
                                 OpenClientCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::OpenClientResponse> buf;

  // Only controlling clients (and the server itself) may create new clients
  if (!client_->is_control()) {
    completer.Reply(buf.view(), ZX_ERR_ACCESS_DENIED);
    return;
  }

  // Clients may not create controlling clients
  if (id == 0) {
    completer.Reply(buf.view(), ZX_ERR_INVALID_ARGS);
    return;
  }

  zx_status_t status = client_->server()->CreateClient(id, std::move(client));
  completer.Reply(buf.view(), status);
}

void PtyClientDevice::ClrSetFeature(uint32_t clr, uint32_t set,
                                    ClrSetFeatureCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ClrSetFeatureResponse> buf;

  constexpr uint32_t kAllowedFeatureBits = ::llcpp::fuchsia::hardware::pty::FEATURE_RAW;

  zx_status_t status = ZX_OK;
  if ((clr & ~kAllowedFeatureBits) || (set & ~kAllowedFeatureBits)) {
    status = ZX_ERR_NOT_SUPPORTED;
  } else {
    client_->ClearSetFlags(clr, set);
  }
  completer.Reply(buf.view(), status, client_->flags());
}

void PtyClientDevice::GetWindowSize(GetWindowSizeCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::GetWindowSizeResponse> buf;
  auto size = client_->server()->window_size();
  ::llcpp::fuchsia::hardware::pty::WindowSize wsz = {.width = size.width, .height = size.height};
  completer.Reply(buf.view(), ZX_OK, wsz);
}

void PtyClientDevice::MakeActive(uint32_t client_pty_id, MakeActiveCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::MakeActiveResponse> buf;

  if (!client_->is_control()) {
    completer.Reply(buf.view(), ZX_ERR_ACCESS_DENIED);
    return;
  }

  zx_status_t status = client_->server()->MakeActive(client_pty_id);
  completer.Reply(buf.view(), status);
}

void PtyClientDevice::ReadEvents(ReadEventsCompleter::Sync completer) {
  fidl::Buffer<::llcpp::fuchsia::hardware::pty::Device::ReadEventsResponse> buf;

  if (!client_->is_control()) {
    completer.Reply(buf.view(), ZX_ERR_ACCESS_DENIED, 0);
    return;
  }

  uint32_t events = client_->server()->DrainEvents();
  completer.Reply(buf.view(), ZX_OK, events);
}

// Assert in all of these, since these should be handled by fs::Connection before our
// HandleFsSpecificMessage() is called.
void PtyClientDevice::Read(uint64_t count, ReadCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyClientDevice::Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Clone(uint32_t flags, zx::channel node, CloneCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Close(CloseCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyClientDevice::Describe(DescribeCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyClientDevice::GetAttr(GetAttrCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyClientDevice::GetFlags(GetFlagsCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyClientDevice::ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
                              WriteAtCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Seek(int64_t offset, ::llcpp::fuchsia::io::SeekOrigin start,
                           SeekCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Truncate(uint64_t length, TruncateCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::SetFlags(uint32_t flags, SetFlagsCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Sync(SyncCompleter::Sync completer) { ZX_ASSERT(false); }

void PtyClientDevice::SetAttr(uint32_t flags, ::llcpp::fuchsia::io::NodeAttributes attributes,
                              SetAttrCompleter::Sync completer) {
  ZX_ASSERT(false);
}
