// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl-async/cpp/bind.h>
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

zx_status_t Console::Create(async_dispatcher_t* dispatcher, RxSource rx_source, TxSink tx_sink,
                            fbl::RefPtr<Console>* console) {
  zx::eventpair event1, event2;
  zx_status_t status = zx::eventpair::create(0, &event1, &event2);
  if (status != ZX_OK) {
    return status;
  }

  *console = fbl::MakeRefCounted<Console>(dispatcher, std::move(event1), std::move(event2),
                                          std::move(rx_source), std::move(tx_sink));
  return ZX_OK;
}

Console::Console(async_dispatcher_t* dispatcher, zx::eventpair event1, zx::eventpair event2,
                 RxSource rx_source, TxSink tx_sink)
    : dispatcher_(dispatcher),
      rx_fifo_(std::move(event1)),
      rx_event_(std::move(event2)),
      rx_source_(std::move(rx_source)),
      tx_sink_(std::move(tx_sink)),
      rx_thread_(std::thread([this]() { DebugReaderThread(); })) {}

Console::~Console() { rx_thread_.join(); }

zx_status_t Console::Read(void* data, size_t len, size_t offset, size_t* out_actual) {
  // Don't try to read more than the FIFO can hold.
  uint64_t to_read = fbl::min<uint64_t>(len, Fifo::kFifoSize);
  return rx_fifo_.Read(reinterpret_cast<uint8_t*>(data), to_read, out_actual);
}

zx_status_t Console::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  zx_status_t status = ZX_OK;
  size_t total_written = 0;

  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
  size_t count = len;
  while (count > 0) {
    size_t xfer = fbl::min(count, kMaxWriteSize);
    if ((status = tx_sink_(ptr, xfer)) != ZX_OK) {
      break;
    }
    ptr += xfer;
    count -= xfer;
    total_written += xfer;
  }
  if (total_written > 0) {
    status = ZX_OK;
  }
  *out_actual = total_written;
  return status;
}

zx_status_t Console::GetNodeInfo(::llcpp::fuchsia::io::NodeInfo* info) const {
  auto& tty = info->mutable_tty();
  return rx_event_.duplicate(ZX_RIGHTS_BASIC, &tty.event);
}

void Console::DebugReaderThread() {
  while (true) {
    uint8_t ch;
    zx_status_t status = rx_source_(&ch);
    if (status != ZX_OK) {
      return;
    }
    size_t actual;
    rx_fifo_.Write(&ch, 1, &actual);
  }
}
