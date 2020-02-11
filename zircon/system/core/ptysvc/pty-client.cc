// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-client.h"

#include "pty-server.h"

PtyClient::PtyClient(fbl::RefPtr<PtyServer> server, uint32_t id, zx::eventpair local,
                     zx::eventpair remote)
    : server_(std::move(server)), id_(id), local_(std::move(local)), remote_(std::move(remote)) {}

PtyClient::~PtyClient() = default;

zx_status_t PtyClient::Create(fbl::RefPtr<PtyServer> server, uint32_t id,
                              fbl::RefPtr<PtyClient>* out) {
  zx::eventpair local, remote;
  zx_status_t status = zx::eventpair::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  *out = fbl::MakeRefCounted<PtyClient>(std::move(server), id, std::move(local), std::move(remote));
  return ZX_OK;
}

zx_status_t PtyClient::Read(void* data, size_t count, size_t* out_actual) {
  bool was_full = rx_fifo_.is_full();
  size_t length = rx_fifo_.Read(data, count);
  if (rx_fifo_.is_empty()) {
    DeAssertReadableSignal();
  }
  if (was_full && length) {
    server_->AssertWritableSignal();
  }

  if (length > 0) {
    *out_actual = length;
    return ZX_OK;
  } else {
    return is_peer_closed() ? ZX_ERR_PEER_CLOSED : ZX_ERR_SHOULD_WAIT;
  }
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
  zx_status_t status = server_->Recv(buf, count, &length, &is_full);
  if (status == ZX_OK) {
    *actual = length;
  }
  if (is_full) {
    DeAssertWritableSignal();
  }

  return status;
}

void PtyClient::AdjustSignals() {
  zx_signals_t to_clear = 0;
  zx_signals_t to_set = 0;

  if (is_active()) {
    to_set = ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE;
  } else {
    to_clear = ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE;
  }

  if (rx_fifo_.is_empty()) {
    to_clear = ::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE;
  } else {
    to_set = ::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE;
  }

  local_.signal_peer(to_clear, to_set);
}

void PtyClient::Shutdown() { server_->RemoveClient(this); }
