// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-server-device.h"

// The pty server half only supports OpenClient and SetWindowSize. Return ZX_ERR_NOT_SUPPORTED for
// all of the others

void PtyServerDevice::Describe2(Describe2Completer::Sync& completer) {
  zx::eventpair event;
  if (zx_status_t status = server_->GetEvent(&event); status != ZX_OK) {
    completer.Close(status);
  } else {
    fidl::Arena alloc;
    completer.Reply(fuchsia_hardware_pty::wire::DeviceDescribe2Response::Builder(alloc)
                        .event(std::move(event))
                        .Build());
  }
}

void PtyServerDevice::SetWindowSize(SetWindowSizeRequestView request,
                                    SetWindowSizeCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::SetWindowSize> buf;
  server_->set_window_size({.width = request->size.width, .height = request->size.height});
  completer.buffer(buf.view()).Reply(ZX_OK);
}
void PtyServerDevice::OpenClient(OpenClientRequestView request,
                                 OpenClientCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::OpenClient> buf;
  completer.buffer(buf.view())
      .Reply(server_->CreateClient(request->id, std::move(request->client)));
}

void PtyServerDevice::ClrSetFeature(ClrSetFeatureRequestView request,
                                    ClrSetFeatureCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::ClrSetFeature> buf;
  completer.buffer(buf.view()).Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void PtyServerDevice::GetWindowSize(GetWindowSizeCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::GetWindowSize> buf;
  fuchsia_hardware_pty::wire::WindowSize wsz = {.width = 0, .height = 0};
  completer.buffer(buf.view()).Reply(ZX_ERR_NOT_SUPPORTED, wsz);
}

void PtyServerDevice::MakeActive(MakeActiveRequestView request,
                                 MakeActiveCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::MakeActive> buf;
  completer.buffer(buf.view()).Reply(ZX_ERR_NOT_SUPPORTED);
}

void PtyServerDevice::ReadEvents(ReadEventsCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::ReadEvents> buf;
  completer.buffer(buf.view()).Reply(ZX_ERR_NOT_SUPPORTED, 0);
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

void PtyServerDevice::Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::Close(CloseCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::Query(QueryCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::GetAttr(GetAttrCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::GetFlags(GetFlagsCompleter::Sync& completer) { ZX_ASSERT(false); }

void PtyServerDevice::SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyServerDevice::QueryFilesystem(QueryFilesystemCompleter::Sync& completer) {
  ZX_ASSERT(false);
}
