// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-client.h"

#include <fidl/fuchsia.io/cpp/wire.h>

#include <utility>

#include "pty-server.h"

PtyClient::PtyClient(std::shared_ptr<PtyServer> server, uint32_t id, zx::eventpair local,
                     zx::eventpair remote)
    : server_(std::move(server)), id_(id), local_(std::move(local)), remote_(std::move(remote)) {}

void PtyClient::AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device> request) {
  const zx_handle_t key = request.channel().get();
  auto [it, inserted] = bindings_.insert(
      {key, fidl::BindServer(server().dispatcher(), std::move(request), this,
                             [](PtyClient* impl, fidl::UnbindInfo,
                                fidl::ServerEnd<fuchsia_hardware_pty::Device> key) {
                               PtyClient& self = *impl;
                               size_t erased = self.bindings_.erase(key.channel().get());
                               ZX_ASSERT_MSG(erased == 1, "erased=%zu", erased);
                               if (self.bindings_.empty()) {
                                 self.server().RemoveClient(self.id_);
                               }
                             })});
  ZX_ASSERT_MSG(inserted, "handle=%d", key);
}

void PtyClient::Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) {
  AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device>(request->request.TakeChannel()));
}

void PtyClient::Close(CloseCompleter::Sync& completer) {
  completer.ReplySuccess();
  completer.Close(ZX_OK);
}

void PtyClient::Query(QueryCompleter::Sync& completer) {
  const std::string_view kProtocol = fuchsia_hardware_pty::wire::kDeviceProtocolName;
  uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
  completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
}

void PtyClient::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  uint8_t data[fuchsia_io::wire::kMaxBuf];
  uint64_t len = std::min(request->count, sizeof(data));
  size_t out_actual;
  if (zx_status_t status = Read(data, len, &out_actual); status != ZX_OK) {
    return completer.ReplyError(status);
  }
  return completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(data, out_actual));
}

void PtyClient::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  size_t out_actual;
  if (zx_status_t status = Write(request->data.data(), request->data.count(), &out_actual);
      status != ZX_OK) {
    return completer.ReplyError(status);
  }
  return completer.ReplySuccess(out_actual);
}

void PtyClient::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  if (request->flags != fuchsia_io::wire::OpenFlags::kCloneSameRights) {
    request->object.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device>(request->object.TakeChannel()));
}

void PtyClient::Describe2(Describe2Completer::Sync& completer) {
  zx::eventpair event;
  if (zx_status_t status = remote_.duplicate(ZX_RIGHTS_BASIC, &event); status != ZX_OK) {
    completer.Close(status);
  } else {
    fidl::Arena alloc;
    completer.Reply(fuchsia_hardware_pty::wire::DeviceDescribe2Response::Builder(alloc)
                        .event(std::move(event))
                        .Build());
  }
}

void PtyClient::SetWindowSize(SetWindowSizeRequestView request,
                              SetWindowSizeCompleter::Sync& completer) {
  server().SetWindowSize(request, completer);
}

void PtyClient::OpenClient(OpenClientRequestView request, OpenClientCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::OpenClient> buf;

  // Only controlling clients (and the server itself) may create new clients
  if (!is_control()) {
    completer.buffer(buf.view()).Reply(ZX_ERR_ACCESS_DENIED);
    return;
  }

  // Clients may not create controlling clients
  if (request->id == 0) {
    completer.buffer(buf.view()).Reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx_status_t status = server().CreateClient(request->id, std::move(request->client));
  completer.buffer(buf.view()).Reply(status);
}

void PtyClient::ClrSetFeature(ClrSetFeatureRequestView request,
                              ClrSetFeatureCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::ClrSetFeature> buf;

  constexpr uint32_t kAllowedFeatureBits = fuchsia_hardware_pty::wire::kFeatureRaw;

  zx_status_t status = ZX_OK;
  if ((request->clr & ~kAllowedFeatureBits) || (request->set & ~kAllowedFeatureBits)) {
    status = ZX_ERR_NOT_SUPPORTED;
  } else {
    ClearSetFlags(request->clr, request->set);
  }
  completer.buffer(buf.view()).Reply(status, flags());
}

void PtyClient::GetWindowSize(GetWindowSizeCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::GetWindowSize> buf;
  auto size = server().window_size();
  fuchsia_hardware_pty::wire::WindowSize wsz = {.width = size.width, .height = size.height};
  completer.buffer(buf.view()).Reply(ZX_OK, wsz);
}

void PtyClient::MakeActive(MakeActiveRequestView request, MakeActiveCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::MakeActive> buf;

  if (!is_control()) {
    completer.buffer(buf.view()).Reply(ZX_ERR_ACCESS_DENIED);
    return;
  }

  zx_status_t status = server().MakeActive(request->client_pty_id);
  completer.buffer(buf.view()).Reply(status);
}

void PtyClient::ReadEvents(ReadEventsCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::ReadEvents> buf;

  if (!is_control()) {
    completer.buffer(buf.view()).Reply(ZX_ERR_ACCESS_DENIED, 0);
    return;
  }

  uint32_t events = server().DrainEvents();
  completer.buffer(buf.view()).Reply(ZX_OK, events);
}

zx_status_t PtyClient::Read(void* data, size_t count, size_t* out_actual) {
  if (count == 0) {
    *out_actual = 0;
    return ZX_OK;
  }

  bool was_full = rx_fifo_.is_full();
  size_t length = rx_fifo_.Read(data, count);
  if (rx_fifo_.is_empty()) {
    DeAssertReadableSignal();
  }
  if (was_full && length) {
    server().AssertWritableSignal();
  }

  if (length > 0) {
    *out_actual = length;
    return ZX_OK;
  }
  return is_peer_closed() ? ZX_ERR_PEER_CLOSED : ZX_ERR_SHOULD_WAIT;
}

zx_status_t PtyClient::Write(const void* data, size_t count, size_t* out_actual) {
  if (is_peer_closed()) {
    return ZX_ERR_PEER_CLOSED;
  }

  if (!is_active()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  if (count == 0) {
    *out_actual = 0;
    return ZX_OK;
  }

  if (in_raw_mode()) {
    return WriteChunk(data, count, out_actual);
  }

  // Since we're not in raw mode, perform \n -> \r\n translation.
  auto chunk_start = static_cast<const uint8_t*>(data);
  auto chunk_end = chunk_start;
  size_t chunk_length;
  size_t chunk_actual;
  size_t sent = 0;

  auto partial_result = [&sent, out_actual](zx_status_t status) {
    if (sent) {
      *out_actual = sent;
      return ZX_OK;
    }
    return status;
  };

  zx_status_t status;
  for (size_t i = 0; i < count; i++) {
    // Iterate until there's a linefeed character.
    if (*chunk_end != '\n') {
      chunk_end++;
      continue;
    }

    // Send up to (but not including) the linefeed.
    chunk_length = chunk_end - chunk_start;
    status = WriteChunk(chunk_start, chunk_length, &chunk_actual);
    if (status != ZX_OK) {
      return partial_result(status);
    }

    sent += chunk_actual;
    if (chunk_actual != chunk_length) {
      return partial_result(status);
    }

    // Send the translated line ending.
    // TODO(fxbug.dev/35945): Prevent torn writes here by wiring through support
    // for Fifo::Write's "atomic" flag.
    status = WriteChunk("\r\n", 2, &chunk_actual);
    if (status != ZX_OK) {
      return partial_result(status);
    }

    // This case means only the \r of the \r\n was sent; report to the caller
    // as if it didn't work at all.
    if (chunk_actual != 2) {
      return partial_result(status);
    }

    // Don't increment for the \r.
    sent++;

    chunk_start = chunk_end + 1;
    chunk_end = chunk_start;
  }

  // Write out the rest of the buffer if necessary.
  chunk_length = chunk_end - chunk_start;
  status = WriteChunk(chunk_start, chunk_length, &chunk_actual);
  if (status == ZX_OK) {
    sent += chunk_actual;
  }

  return partial_result(status);
}

zx_status_t PtyClient::WriteChunk(const void* buf, size_t count, size_t* actual) {
  size_t length;
  bool is_full = false;
  zx_status_t status = server().Recv(buf, count, &length, &is_full);
  if (status == ZX_OK) {
    *actual = length;
  }
  if (is_full) {
    DeAssertWritableSignal();
  }

  return status;
}

void PtyClient::AdjustSignals() {
  fuchsia_device::wire::DeviceSignal to_clear, to_set;

  if (is_active()) {
    to_set = fuchsia_device::wire::DeviceSignal::kWritable;
  } else {
    to_clear = fuchsia_device::wire::DeviceSignal::kWritable;
  }

  if (rx_fifo_.is_empty()) {
    to_clear = fuchsia_device::wire::DeviceSignal::kReadable;
  } else {
    to_set = fuchsia_device::wire::DeviceSignal::kReadable;
  }

  local_.signal_peer(static_cast<zx_signals_t>(to_clear), static_cast<zx_signals_t>(to_set));
}
