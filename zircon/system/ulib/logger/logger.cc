// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/c/fidl.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/logger/logger.h>
#include <lib/syslog/logger.h>
#include <lib/syslog/wire_format.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <utility>

#include <fbl/string_buffer.h>

namespace logger {
namespace {

static fx_log_packet_t packet;

}  // namespace

LoggerImpl::LoggerImpl(zx::channel channel, int out_fd)
    : channel_(std::move(channel)),
      fd_(out_fd),
      wait_(this, channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED),
      socket_wait_(this) {}

LoggerImpl::~LoggerImpl() { fsync(fd_); }

zx_status_t LoggerImpl::Begin(async_dispatcher_t* dispatcher) { return wait_.Begin(dispatcher); }

zx_status_t LoggerImpl::PrintLogMessage(const fx_log_packet_t* packet) {
  fbl::StringBuffer<sizeof(fx_log_packet_t) + 100> buf;  // should be enough to log message
  buf.AppendPrintf("[%05ld.%06ld]", packet->metadata.time / 1000000000UL,
                   (packet->metadata.time / 1000UL) % 1000000UL);
  buf.AppendPrintf("[%ld]", packet->metadata.pid);
  buf.AppendPrintf("[%ld]", packet->metadata.tid);

  // print tags
  size_t pos = 0;
  buf.Append("[");
  unsigned int tag_len = packet->data[pos];
  int i = 1;
  while (tag_len > 0) {
    if (i > FX_LOG_MAX_TAGS || tag_len > FX_LOG_MAX_TAG_LEN) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (i > 1) {
      buf.Append(", ");
    }
    pos = pos + 1;
    buf.Append(packet->data + pos, tag_len);
    pos = pos + tag_len;
    tag_len = packet->data[pos];
    i++;
  }
  buf.Append("]");

  auto severity = packet->metadata.severity;
  switch (severity) {
    case FX_LOG_INFO:
      buf.Append(" INFO");
      break;
    case FX_LOG_WARNING:
      buf.Append(" WARNING");
      break;
    case FX_LOG_ERROR:
      buf.Append(" ERROR");
      break;
    case FX_LOG_FATAL:
      buf.Append(" FATAL");
      break;
    default:
      buf.AppendPrintf(" VLOG(%d)", -severity);
  }
  buf.Append(": ");
  pos++;  // point to log msg
  buf.Append(packet->data + pos);
  buf.Append("\n");

  ssize_t status = write(fd_, buf.data(), buf.size());
  if (status < 0) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

void LoggerImpl::OnLogMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                              zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    NotifyError(status);
    return;
  }

  if (signal->observed & ZX_SOCKET_READABLE) {
    memset(&packet, 0, sizeof(packet));
    status = socket_.read(0, &packet, sizeof(packet), nullptr);
    if (status != ZX_OK) {
      NotifyError(status);
      return;
    }
    if (status != ZX_ERR_SHOULD_WAIT) {
      // set last byte of packet to zero so that we don't overflow buffer while
      // reading message.
      packet.data[sizeof(packet.data) - 1] = 0;
      status = PrintLogMessage(&packet);
      if (status == ZX_ERR_INVALID_ARGS) {
        NotifyError(status);
        return;
      }
    }
    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      NotifyError(status);
    }
    return;
  }

  ZX_DEBUG_ASSERT(signal->observed & ZX_SOCKET_PEER_CLOSED);
  NotifyError(ZX_ERR_PEER_CLOSED);
}

void LoggerImpl::OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                               zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    NotifyError(status);
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    fidl::MessageBuffer buffer;
    for (uint64_t i = 0; i < signal->count; i++) {
      status = ReadAndDispatchMessage(&buffer, dispatcher);
      if (status == ZX_ERR_SHOULD_WAIT)
        break;
      if (status != ZX_OK) {
        NotifyError(status);
        return;
      }
    }
    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      NotifyError(status);
    }
    return;
  }

  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  channel_.reset();
  if (!socket_) {
    // if there is no socket, it doesn't make sense to keep running this
    // instance.
    NotifyError(ZX_ERR_PEER_CLOSED);
  }
}

zx_status_t LoggerImpl::ReadAndDispatchMessage(fidl::MessageBuffer* buffer,
                                               async_dispatcher_t* dispatcher) {
  fidl::HLCPPIncomingMessage message = buffer->CreateEmptyIncomingMessage();
  zx_status_t status = message.Read(channel_.get(), 0);
  if (status != ZX_OK)
    return status;

  uint64_t ordinal = message.ordinal();
  switch (ordinal) {
    case fuchsia_logger_LogSinkConnectOrdinal: {
      return Connect(std::move(message), dispatcher);
      default:
        fprintf(stderr, "logger: error: Unknown message ordinal: %lu\n", ordinal);
        return ZX_ERR_NOT_SUPPORTED;
    }  // switch
  }
}

zx_status_t LoggerImpl::Connect(fidl::HLCPPIncomingMessage message,
                                async_dispatcher_t* dispatcher) {
  if (socket_) {
    return ZX_ERR_INVALID_ARGS;
  }
  const char* error_msg = nullptr;
  zx_status_t status = message.Decode(&fuchsia_logger_LogSinkConnectRequestTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "logger: error: Connect: %s\n", error_msg);
    return status;
  }
  auto* request = message.GetBytesAs<fuchsia_logger_LogSinkConnectRequest>();
  socket_.reset(request->socket);
  socket_wait_.set_object(socket_.get());
  socket_wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);
  socket_wait_.Begin(dispatcher);
  return ZX_OK;
}

void LoggerImpl::NotifyError(zx_status_t error) {
  socket_wait_.Cancel();
  wait_.Cancel();
  channel_.reset();
  socket_.reset();
  if (error_handler_)
    error_handler_(error);
}

}  // namespace logger
