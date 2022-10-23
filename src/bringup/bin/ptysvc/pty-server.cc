// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-server.h"

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/zx/eventpair.h>

#include "pty-client.h"

zx::result<PtyServer::Args> PtyServer::Args::Create() {
  PtyServer::Args args;
  if (zx_status_t status = zx::eventpair::create(0, &args.local_, &args.remote_); status != ZX_OK) {
    return zx::error(status);
  }
  // Create the FIFO in the "hung-up" state.  Note that this is
  // considered "readable" so that clients will try to read and see an
  // EOF condition via a 0-byte response with ZX_OK.
  if (zx_status_t status = args.local_.signal_peer(
          0, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable |
                                       fuchsia_device::wire::DeviceSignal::kHangup));
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(args));
}

PtyServer::PtyServer(Args args, async_dispatcher_t* dispatcher)
    : local_(std::move(args.local_)), remote_(std::move(args.remote_)), dispatcher_(dispatcher) {}

PtyServer::~PtyServer() = default;

void PtyServer::AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device> request) {
  const zx_handle_t key = request.channel().get();
  auto [it, inserted] = bindings_.insert(
      {key, fidl::BindServer(dispatcher(), std::move(request), shared_from_this(),
                             [](PtyServer* impl, fidl::UnbindInfo,
                                fidl::ServerEnd<fuchsia_hardware_pty::Device> key) {
                               PtyServer& self = *impl;
                               size_t erased = self.bindings_.erase(key.channel().get());
                               ZX_ASSERT_MSG(erased == 1, "erased=%zu", erased);
                               if (self.bindings_.empty()) {
                                 for (auto& [id, client] : self.clients_) {
                                   // inform clients that server is gone
                                   client.AssertHangupSignal();
                                 }
                                 self.active_.reset();
                               }
                             })});
  ZX_ASSERT_MSG(inserted, "handle=%d", key);
}

void PtyServer::Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) {
  AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device>(request->request.TakeChannel()));
}

void PtyServer::Close(CloseCompleter::Sync& completer) {
  completer.ReplySuccess();
  completer.Close(ZX_OK);
}

void PtyServer::Query(QueryCompleter::Sync& completer) {
  const std::string_view kProtocol = fuchsia_hardware_pty::wire::kDeviceProtocolName;
  uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
  completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
}

void PtyServer::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  uint8_t data[fuchsia_io::wire::kMaxBuf];
  uint64_t len = std::min(request->count, sizeof(data));
  size_t out_actual;
  if (zx_status_t status = Read(data, len, &out_actual); status != ZX_OK) {
    return completer.ReplyError(status);
  }
  return completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(data, out_actual));
}

void PtyServer::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  size_t out_actual;
  if (zx_status_t status = Write(request->data.data(), request->data.count(), &out_actual);
      status != ZX_OK) {
    return completer.ReplyError(status);
  }
  return completer.ReplySuccess(out_actual);
}

void PtyServer::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  if (request->flags != fuchsia_io::wire::OpenFlags::kCloneSameRights) {
    request->object.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device>(request->object.TakeChannel()));
}

void PtyServer::Describe2(Describe2Completer::Sync& completer) {
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

void PtyServer::OpenClient(OpenClientRequestView request, OpenClientCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::OpenClient> buf;
  completer.buffer(buf.view()).Reply(CreateClient(request->id, std::move(request->client)));
}

void PtyServer::ClrSetFeature(ClrSetFeatureRequestView request,
                              ClrSetFeatureCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::ClrSetFeature> buf;
  completer.buffer(buf.view()).Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void PtyServer::GetWindowSize(GetWindowSizeCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::GetWindowSize> buf;
  completer.buffer(buf.view()).Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void PtyServer::MakeActive(MakeActiveRequestView request, MakeActiveCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::MakeActive> buf;
  completer.buffer(buf.view()).Reply(ZX_ERR_NOT_SUPPORTED);
}

void PtyServer::ReadEvents(ReadEventsCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::ReadEvents> buf;
  completer.buffer(buf.view()).Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void PtyServer::SetWindowSize(SetWindowSizeRequestView request,
                              SetWindowSizeCompleter::Sync& completer) {
  fidl::ServerBuffer<fuchsia_hardware_pty::Device::SetWindowSize> buf;
  size_ = request->size;
  events_ |= fuchsia_hardware_pty::wire::kEventWindowSize;
  if (control_.has_value()) {
    control_.value().get().AssertEventSignal();
  }
  completer.buffer(buf.view()).Reply(ZX_OK);
}

zx_status_t PtyServer::Read(void* data, size_t count, size_t* out_actual) {
  if (count == 0) {
    *out_actual = 0;
    return ZX_OK;
  }

  bool eof = false;

  bool was_full = rx_fifo_.is_full();
  size_t length = rx_fifo_.Read(data, count);
  if (rx_fifo_.is_empty()) {
    if (clients_.empty()) {
      eof = true;
    } else if (length > 0) {
      // We only need to clear the READABLE signal if we read anything.
      local_.signal_peer(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                         0);
    }
  }
  if (was_full && length > 0) {
    if (active_.has_value()) {
      active_.value().get().AssertWritableSignal();
    }
  }

  if (length > 0) {
    *out_actual = length;
    return ZX_OK;
  }
  if (eof) {
    *out_actual = 0;
    return ZX_OK;
  }
  return ZX_ERR_SHOULD_WAIT;
}

zx_status_t PtyServer::Write(const void* data, size_t count, size_t* out_actual) {
  size_t length;
  if (zx_status_t status = Send(data, count, &length); status != ZX_OK) {
    return status;
  }
  *out_actual = length;
  return ZX_OK;
}

zx_status_t PtyServer::CreateClient(uint32_t id,
                                    fidl::ServerEnd<fuchsia_hardware_pty::Device> client_request) {
  // Make sure we don't already have a client with the requested id.
  if (clients_.find(id) != clients_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx::eventpair local, remote;
  if (zx_status_t status = zx::eventpair::create(0, &local, &remote); status != ZX_OK) {
    return status;
  }

  if (clients_.empty()) {
    // if there were no clients, make sure we take server
    // out of HANGUP and READABLE, where it landed if all
    // its clients had closed
    if (zx_status_t status = local_.signal_peer(
            static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable |
                                      fuchsia_device::wire::DeviceSignal::kHangup),
            0);
        status != ZX_OK) {
      return status;
    }
  }

  const auto [it, inserted] =
      clients_.try_emplace(id, shared_from_this(), id, std::move(local), std::move(remote));
  ZX_ASSERT(inserted);
  PtyClient& client = it->second;
  client.AddConnection(std::move(client_request));

  if (!active_.has_value()) {
    MakeActive(client);
  }
  if (id == 0) {
    control_ = client;
    if (events_) {
      client.AssertEventSignal();
    }
  }

  client.AdjustSignals();
  return ZX_OK;
}

void PtyServer::RemoveClient(uint32_t id) {
  auto client_node = clients_.extract(id);
  ZX_ASSERT(!client_node.empty());
  PtyClient& client = client_node.mapped();
  if (client.is_control()) {
    control_.reset();
  }

  if (client.is_active()) {
    // signal controlling client, if there is one
    if (control_.has_value()) {
      // Note that in the implementation this is ported from, DEVICE_SIGNAL_HANGUP is never
      // cleared after being asserted by this?  This seems likely to be a bug.
      control_.value().get().AssertActiveHungup();
    }
    active_.reset();
  }

  // signal server, if the last client has gone away
  if (clients_.empty()) {
    local_.signal_peer(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable),
                       static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable |
                                                 fuchsia_device::wire::DeviceSignal::kHangup));
  }
}

zx_status_t PtyServer::Recv(const void* data, size_t len, size_t* actual, bool* is_full) {
  if (len == 0) {
    *actual = 0;
    return ZX_OK;
  }

  bool was_empty = rx_fifo_.is_empty();
  *actual = rx_fifo_.Write(data, len, false);
  if (was_empty && *actual) {
    local_.signal_peer(0, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable));
  }

  *is_full = rx_fifo_.is_full();

  if (*actual == 0) {
    return ZX_ERR_SHOULD_WAIT;
  }
  return ZX_OK;
}

zx_status_t PtyServer::Send(const void* data, size_t len, size_t* actual) {
  if (!active_.has_value()) {
    *actual = 0;
    return ZX_ERR_PEER_CLOSED;
  }

  if (len == 0) {
    *actual = 0;
    return ZX_OK;
  }

  PtyClient& active = active_.value().get();
  Fifo* client_fifo = active.rx_fifo();
  if (client_fifo->is_full()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  bool was_empty = client_fifo->is_empty();
  if (active.in_raw_mode()) {
    *actual = client_fifo->Write(data, len, false);
  } else {
    if (len > Fifo::kSize) {
      len = Fifo::kSize;
    }
    auto ch = static_cast<const uint8_t*>(data);
    unsigned n = 0;
    unsigned evt = 0;
    while (n < len) {
      // The ASCII code that Ctrl-C generates
      constexpr uint8_t kCtrlC = 0x3;
      if (*ch++ == kCtrlC) {
        evt = fuchsia_hardware_pty::wire::kEventInterrupt;
        break;
      }
      n++;
    }
    size_t r = client_fifo->Write(data, n, false);
    if ((r == n) && evt) {
      // consume the event
      r++;
      events_ |= evt;
      if (control_.has_value()) {
        control_.value().get().AssertEventSignal();
      }
    }
    *actual = r;
  }
  if (was_empty && !client_fifo->is_empty()) {
    active.AssertReadableSignal();
  }
  if (client_fifo->is_full()) {
    local_.signal_peer(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable), 0);
  }
  return ZX_OK;
}

zx_status_t PtyServer::MakeActive(uint32_t id) {
  const auto it = clients_.find(id);
  if (it == clients_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  MakeActive(it->second);
  return ZX_OK;
}

void PtyServer::MakeActive(PtyClient& client) {
  if (active_.has_value() && &active_.value().get() == &client) {
    return;
  }
  if (std::optional active = std::exchange(active_, client); active.has_value()) {
    active.value().get().DeAssertWritableSignal();
  }
  client.AssertWritableSignal();

  fuchsia_device::wire::DeviceSignal to_clear = fuchsia_device::wire::DeviceSignal::kHangup;
  fuchsia_device::wire::DeviceSignal to_set;
  if (client.rx_fifo()->is_full()) {
    to_clear |= fuchsia_device::wire::DeviceSignal::kWritable;
  } else {
    to_set |= fuchsia_device::wire::DeviceSignal::kWritable;
  }

  local_.signal_peer(static_cast<zx_signals_t>(to_clear), static_cast<zx_signals_t>(to_set));
}

uint32_t PtyServer::DrainEvents() {
  uint32_t events = events_;
  events_ = 0;
  if (!active_.has_value()) {
    events |= fuchsia_hardware_pty::wire::kEventHangup;
  }
  if (control_.has_value()) {
    control_.value().get().DeAssertEventSignal();
  }
  return events;
}
