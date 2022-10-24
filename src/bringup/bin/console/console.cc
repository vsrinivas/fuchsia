// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <iterator>
#include <thread>

#include <fbl/string_buffer.h>

Console::Console(async_dispatcher_t* dispatcher, zx::eventpair event1, zx::eventpair event2,
                 RxSource rx_source, TxSink tx_sink, std::vector<std::string> denied_log_tags)
    : dispatcher_(dispatcher),
      rx_fifo_(std::move(event1)),
      rx_event_(std::move(event2)),
      rx_source_(std::move(rx_source)),
      tx_sink_(std::move(tx_sink)),
      denied_log_tags_(std::move(denied_log_tags)),
      rx_thread_(std::thread([this]() { DebugReaderThread(); })) {}

Console::~Console() { rx_thread_.join(); }

void Console::Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) {
  fidl::BindServer(dispatcher_,
                   fidl::ServerEnd<fuchsia_hardware_pty::Device>(request->request.TakeChannel()),
                   static_cast<fidl::WireServer<fuchsia_hardware_pty::Device>*>(this));
}

void Console::Close(CloseCompleter::Sync& completer) {
  completer.ReplySuccess();
  completer.Close(ZX_OK);
}

void Console::Query(QueryCompleter::Sync& completer) {
  const std::string_view kProtocol = fuchsia_hardware_pty::wire::kDeviceProtocolName;
  uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
  completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
}

void Console::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  // Don't try to read more than the FIFO can hold.
  uint64_t to_read = std::min<uint64_t>(request->count, Fifo::kFifoSize);
  uint8_t buf[to_read];
  size_t out_actual;
  if (zx_status_t status = rx_fifo_.Read(buf, to_read, &out_actual); status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(buf, out_actual));
  }
}

void Console::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  cpp20::span span = request->data.get();
  while (!span.empty()) {
    size_t count = std::min(span.size(), kMaxWriteSize);
    if (zx_status_t status = tx_sink_(span.data(), count); status != ZX_OK) {
      uint64_t written = std::distance(request->data.begin(), span.begin());
      if (written != 0) {
        return completer.ReplySuccess(written);
      }
      return completer.ReplyError(status);
    }
    span = span.subspan(count);
  }
  return completer.ReplySuccess(request->data.count());
}

void Console::Describe(DescribeCompleter::Sync& completer) {
  zx::eventpair event;
  if (zx_status_t status = rx_event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &event); status != ZX_OK) {
    completer.Close(status);
  } else {
    fidl::Arena alloc;
    completer.Reply(fuchsia_hardware_pty::wire::DeviceDescribeResponse::Builder(alloc)
                        .event(std::move(event))
                        .Build());
  }
}

void Console::OpenClient(OpenClientRequestView request, OpenClientCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}
void Console::ClrSetFeature(ClrSetFeatureRequestView request,
                            ClrSetFeatureCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}
void Console::GetWindowSize(GetWindowSizeCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}
void Console::MakeActive(MakeActiveRequestView request, MakeActiveCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}
void Console::ReadEvents(ReadEventsCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}
void Console::SetWindowSize(SetWindowSizeRequestView request,
                            SetWindowSizeCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
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

zx_status_t Console::Log(fuchsia_logger::wire::LogMessage log) {
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
    case static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kTrace):
      buffer.Append("] TRACE: ");
      break;
    case static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kDebug):
      buffer.Append("] DEBUG: ");
      break;
    case static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kInfo):
      buffer.Append("] INFO: ");
      break;
    case static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kWarn):
      buffer.Append("] WARNING: ");
      break;
    case static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kError):
      buffer.Append("] ERROR: ");
      break;
    case static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kFatal):
      buffer.Append("] FATAL: ");
      break;
    default:
      buffer.AppendPrintf(
          "] VLOG(%d): ",
          static_cast<int32_t>(fuchsia_logger::LogLevelFilter::kInfo) - log.severity);
  }
  buffer.Append(log.msg.data(), log.msg.size()).Append('\n');
  return tx_sink_(reinterpret_cast<const uint8_t*>(buffer.data()), buffer.size());
}

void Console::Log(LogRequestView request, LogCompleter::Sync& completer) {
  zx_status_t status = Log(request->log);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  completer.Reply();
}

void Console::LogMany(LogManyRequestView request, LogManyCompleter::Sync& completer) {
  for (auto& log : request->log) {
    zx_status_t status = Log(log);
    if (status != ZX_OK) {
      completer.Close(status);
      return;
    }
  }
  completer.Reply();
}

void Console::Done(DoneCompleter::Sync& completer) {}
