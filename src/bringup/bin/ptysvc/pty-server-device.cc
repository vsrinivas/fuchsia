// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-server-device.h"

#include "pty-client.h"

// The pty server half only supports OpenClient and SetWindowSize. Return ZX_ERR_NOT_SUPPORTED for
// all of the others

void PtyServerDevice::SetWindowSize(SetWindowSizeRequestView request,
                                    SetWindowSizeCompleter::Sync& completer) {
  fidl::Buffer<fidl::WireResponse<fuchsia_hardware_pty::Device::SetWindowSize>> buf;
  server_->set_window_size({.width = request->size.width, .height = request->size.height});
  completer.Reply(buf.view(), ZX_OK);
}
void PtyServerDevice::OpenClient(OpenClientRequestView request,
                                 OpenClientCompleter::Sync& completer) {
  fidl::Buffer<fidl::WireResponse<fuchsia_hardware_pty::Device::OpenClient>> buf;
  completer.Reply(buf.view(), server_->CreateClient(request->id, std::move(request->client)));
}

void PtyServerDevice::ClrSetFeature(ClrSetFeatureRequestView request,
                                    ClrSetFeatureCompleter::Sync& completer) {
  fidl::Buffer<fidl::WireResponse<fuchsia_hardware_pty::Device::ClrSetFeature>> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

void PtyServerDevice::GetWindowSize(GetWindowSizeRequestView request,
                                    GetWindowSizeCompleter::Sync& completer) {
  fidl::Buffer<fidl::WireResponse<fuchsia_hardware_pty::Device::GetWindowSize>> buf;
  fuchsia_hardware_pty::wire::WindowSize wsz = {.width = 0, .height = 0};
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, wsz);
}

void PtyServerDevice::MakeActive(MakeActiveRequestView request,
                                 MakeActiveCompleter::Sync& completer) {
  fidl::Buffer<fidl::WireResponse<fuchsia_hardware_pty::Device::MakeActive>> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED);
}

void PtyServerDevice::ReadEvents(ReadEventsRequestView request,
                                 ReadEventsCompleter::Sync& completer) {
  fidl::Buffer<fidl::WireResponse<fuchsia_hardware_pty::Device::ReadEvents>> buf;
  completer.Reply(buf.view(), ZX_ERR_NOT_SUPPORTED, 0);
}

// Assert in all of these, since these should be handled by fs::Connection before our
// HandleFsSpecificMessage() is called.
void PtyServerDevice::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Close(CloseRequestView request, CloseCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Close2(Close2RequestView request, Close2Completer::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::GetFlags(GetFlagsRequestView request, GetFlagsCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Seek(SeekRequestView request, SeekCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Truncate(TruncateRequestView request, TruncateCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Sync(SyncRequestView request, SyncCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) {
  ZX_ASSERT(false);
}
