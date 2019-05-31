// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_listener_log_sink.h"

#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/logger.h>
#include <lib/syslog/wire_format.h>

namespace netemul {
namespace internal {

/*
 * LogListenerLogSinkImpl
 *
 * public
 */

LogListenerLogSinkImpl::LogListenerLogSinkImpl(
    fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
    std::string prefix, zx::socket log_sink, async_dispatcher_t* dispatcher)
    : LogListenerImpl(std::move(request), "@" + prefix, dispatcher) {
  if (log_sink.is_valid()) {
    log_sock_ = std::move(log_sink);
  } else {
    auto svc_dir = sys::ServiceDirectory::CreateFromNamespace();
    fuchsia::logger::LogSinkPtr log;
    svc_dir->Connect(log.NewRequest(dispatcher));
    zx::socket out;
    zx::socket::create(ZX_SOCKET_DATAGRAM, &out, &log_sock_);
    log->Connect(std::move(out));
  }
}

/*
 * LogListenerLogSinkImpl
 *
 * protected
 */

void LogListenerLogSinkImpl::LogImpl(fuchsia::logger::LogMessage m) {
  fx_log_packet_t packet;
  packet.metadata.dropped_logs = dropped_logs_;
  packet.metadata.pid = m.pid;
  packet.metadata.tid = m.tid;
  packet.metadata.severity = m.severity;
  packet.metadata.time = m.time;
  bool inline_prefix = true;
  if (m.tags.size() < FX_LOG_MAX_TAGS) {
    m.tags.push_back(prefix_);
    inline_prefix = false;
  }

  // insert tags:
  size_t pos = 0;
  for (const auto& tag : m.tags) {
    auto len = tag.length();
    if (len > FX_LOG_MAX_TAG_LEN - 1) {
      len = FX_LOG_MAX_TAG_LEN - 1;
    }
    packet.data[pos] = len;
    pos++;
    memcpy(&packet.data[pos], tag.c_str(), len);
    pos += len;
  }
  packet.data[pos++] = 0;
  if (inline_prefix && prefix_.length() + 4 + pos < sizeof(packet.data)) {
    packet.data[pos++] = '[';
    memcpy(&packet.data[pos], prefix_.c_str(), prefix_.length());
    pos += prefix_.length();
    packet.data[pos++] = ']';
    packet.data[pos++] = ' ';
  }
  ZX_ASSERT(pos <= sizeof(packet.data));
  auto remain = sizeof(packet.data) - pos;
  if (m.msg.length() + 1 < remain) {
    remain = m.msg.length() + 1;
  }
  if (remain > 0) {
    memcpy(&packet.data[pos], m.msg.c_str(), remain);
    packet.data[pos + remain - 1] = 0;
    pos += remain;
  }

  size_t actual;
  pos += sizeof(packet.metadata);
  if (log_sock_.write(0, &packet, pos, &actual) == ZX_OK && actual == pos) {
    dropped_logs_ = 0;
  } else {
    dropped_logs_++;
  }
}

}  // namespace internal
}  // namespace netemul
