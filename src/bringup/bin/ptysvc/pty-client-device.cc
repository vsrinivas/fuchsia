// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-client-device.h"

#include "pty-client.h"

void PtyClientDevice::SetWindowSize(SetWindowSizeRequestView request,
                                    SetWindowSizeCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::SetWindowSize> buf;
  client_->server()->set_window_size(
      {.width = request->size.width, .height = request->size.height});
  completer.buffer(buf.view()).Reply(ZX_OK);
}

void PtyClientDevice::OpenClient(OpenClientRequestView request,
                                 OpenClientCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::OpenClient> buf;

  // Only controlling clients (and the server itself) may create new clients
  if (!client_->is_control()) {
    completer.buffer(buf.view()).Reply(ZX_ERR_ACCESS_DENIED);
    return;
  }

  // Clients may not create controlling clients
  if (request->id == 0) {
    completer.buffer(buf.view()).Reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx_status_t status = client_->server()->CreateClient(request->id, std::move(request->client));
  completer.buffer(buf.view()).Reply(status);
}

void PtyClientDevice::ClrSetFeature(ClrSetFeatureRequestView request,
                                    ClrSetFeatureCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::ClrSetFeature> buf;

  constexpr uint32_t kAllowedFeatureBits = fuchsia_hardware_pty::wire::kFeatureRaw;

  zx_status_t status = ZX_OK;
  if ((request->clr & ~kAllowedFeatureBits) || (request->set & ~kAllowedFeatureBits)) {
    status = ZX_ERR_NOT_SUPPORTED;
  } else {
    client_->ClearSetFlags(request->clr, request->set);
  }
  completer.buffer(buf.view()).Reply(status, client_->flags());
}

void PtyClientDevice::GetWindowSize(GetWindowSizeRequestView request,
                                    GetWindowSizeCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::GetWindowSize> buf;
  auto size = client_->server()->window_size();
  fuchsia_hardware_pty::wire::WindowSize wsz = {.width = size.width, .height = size.height};
  completer.buffer(buf.view()).Reply(ZX_OK, wsz);
}

void PtyClientDevice::MakeActive(MakeActiveRequestView request,
                                 MakeActiveCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::MakeActive> buf;

  if (!client_->is_control()) {
    completer.buffer(buf.view()).Reply(ZX_ERR_ACCESS_DENIED);
    return;
  }

  zx_status_t status = client_->server()->MakeActive(request->client_pty_id);
  completer.buffer(buf.view()).Reply(status);
}

void PtyClientDevice::ReadEvents(ReadEventsRequestView request,
                                 ReadEventsCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::ReadEvents> buf;

  if (!client_->is_control()) {
    completer.buffer(buf.view()).Reply(ZX_ERR_ACCESS_DENIED, 0);
    return;
  }

  uint32_t events = client_->server()->DrainEvents();
  completer.buffer(buf.view()).Reply(ZX_OK, events);
}

// Assert in all of these, since these should be handled by fs::Connection before our
// HandleFsSpecificMessage() is called.
void PtyClientDevice::ReadDeprecated(ReadDeprecatedRequestView request,
                                     ReadDeprecatedCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::WriteDeprecated(WriteDeprecatedRequestView request,
                                      WriteDeprecatedCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::AdvisoryLock(AdvisoryLockRequestView request,
                                   AdvisoryLockCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::CloseDeprecated(CloseDeprecatedRequestView request,
                                      CloseDeprecatedCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Close(CloseRequestView request, CloseCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Describe2(Describe2RequestView request, Describe2Completer::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::GetFlagsDeprecatedUseNode(
    GetFlagsDeprecatedUseNodeRequestView request,
    GetFlagsDeprecatedUseNodeCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::ReadAtDeprecated(ReadAtDeprecatedRequestView request,
                                       ReadAtDeprecatedCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::WriteAtDeprecated(WriteAtDeprecatedRequestView request,
                                        WriteAtDeprecatedCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::SeekDeprecated(SeekDeprecatedRequestView request,
                                     SeekDeprecatedCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Seek(SeekRequestView request, SeekCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Truncate(TruncateRequestView request, TruncateCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Resize(ResizeRequestView request, ResizeCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::SetFlagsDeprecatedUseNode(
    SetFlagsDeprecatedUseNodeRequestView request,
    SetFlagsDeprecatedUseNodeCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::GetBackingMemory(GetBackingMemoryRequestView request,
                                       GetBackingMemoryCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::SyncDeprecated(SyncDeprecatedRequestView request,
                                     SyncDeprecatedCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::Sync(SyncRequestView request, SyncCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::GetFlags(GetFlagsRequestView request, GetFlagsCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) {
  ZX_ASSERT(false);
}

void PtyClientDevice::QueryFilesystem(QueryFilesystemRequestView request,
                                      QueryFilesystemCompleter::Sync& completer) {
  ZX_ASSERT(false);
}
