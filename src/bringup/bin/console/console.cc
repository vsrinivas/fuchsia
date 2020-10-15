// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/logger.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <thread>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>

zx_status_t Console::Create(RxSource rx_source, TxSink tx_sink,
                            std::vector<std::string> denied_log_tags,
                            fbl::RefPtr<Console>* console) {
  zx::eventpair event1, event2;
  zx_status_t status = zx::eventpair::create(0, &event1, &event2);
  if (status != ZX_OK) {
    return status;
  }

  *console =
      fbl::MakeRefCounted<Console>(std::move(event1), std::move(event2), std::move(rx_source),
                                   std::move(tx_sink), std::move(denied_log_tags));
  return ZX_OK;
}

Console::Console(zx::eventpair event1, zx::eventpair event2, RxSource rx_source, TxSink tx_sink,
                 std::vector<std::string> denied_log_tags)
    : rx_fifo_(std::move(event1)),
      rx_event_(std::move(event2)),
      rx_source_(std::move(rx_source)),
      tx_sink_(std::move(tx_sink)),
      denied_log_tags_(std::move(denied_log_tags)),
      rx_thread_(std::thread([this]() { DebugReaderThread(); })) {}

Console::~Console() { rx_thread_.join(); }

zx_status_t Console::Read(void* data, size_t len, size_t* out_actual) {
  // Don't try to read more than the FIFO can hold.
  uint64_t to_read = std::min<uint64_t>(len, Fifo::kFifoSize);
  return rx_fifo_.Read(reinterpret_cast<uint8_t*>(data), to_read, out_actual);
}

zx_status_t Console::Write(const void* data, size_t len, size_t* out_actual) {
  zx_status_t status = ZX_OK;
  size_t total_written = 0;

  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
  size_t count = len;
  while (count > 0) {
    size_t xfer = std::min(count, kMaxWriteSize);
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

zx_status_t Console::GetEvent(zx::eventpair* event) const {
  return rx_event_.duplicate(ZX_RIGHTS_BASIC, event);
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

zx_status_t Console::Log(llcpp::fuchsia::logger::LogMessage log) {
  fbl::StringBuffer<kMaxWriteSize> buffer;
  auto time = zx::nsec(log.time);
  buffer.AppendPrintf("[%05ld.%03ld] %05lu:%05lu> [", time.to_secs(), time.to_msecs() % 1000,
                      log.pid, log.tid);
  auto count = log.tags.count();
  for (auto& tag : log.tags) {
    for (auto& denied_log_tag : denied_log_tags_) {
      if (strncmp(denied_log_tag.data(), tag.data(), tag.size()) == 0) {
        return ZX_OK;
      }
    }
    buffer.Append(tag.data(), tag.size());
    if (--count > 0) {
      buffer.Append(", ");
    }
  }
  switch (log.severity) {
    case FX_LOG_TRACE:
      buffer.Append("] TRACE: ");
      break;
    case FX_LOG_DEBUG:
      buffer.Append("] DEBUG: ");
      break;
    case FX_LOG_INFO:
      buffer.Append("] INFO: ");
      break;
    case FX_LOG_WARNING:
      buffer.Append("] WARNING: ");
      break;
    case FX_LOG_ERROR:
      buffer.Append("] ERROR: ");
      break;
    case FX_LOG_FATAL:
      buffer.Append("] FATAL: ");
      break;
    default:
      buffer.AppendPrintf("] VLOG(%d): ", FX_LOG_INFO - log.severity);
  }
  buffer.Append(log.msg.data(), log.msg.size()).Append('\n');
  return tx_sink_(reinterpret_cast<const uint8_t*>(buffer.data()), buffer.size());
}

void Console::Log(llcpp::fuchsia::logger::LogMessage log, LogCompleter::Sync& completer) {
  zx_status_t status = Log(std::move(log));
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  completer.Reply();
}

void Console::LogMany(fidl::VectorView<llcpp::fuchsia::logger::LogMessage> logs,
                      LogManyCompleter::Sync& completer) {
  for (auto& log : logs) {
    zx_status_t status = Log(std::move(log));
    if (status != ZX_OK) {
      completer.Close(status);
      return;
    }
  }
  completer.Reply();
}

void Console::Done(DoneCompleter::Sync& completer) {}
