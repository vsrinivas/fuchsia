// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fx_logger.h"

#include <lib/syslog/logger.h>
#include <lib/syslog/wire_format.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <algorithm>
#include <atomic>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/string_buffer.h>

namespace {

// This thread's koid.
// Initialized on first use.
thread_local zx_koid_t tls_thread_koid{ZX_KOID_INVALID};

zx_koid_t GetCurrentThreadKoid() {
  if (unlikely(tls_thread_koid == ZX_KOID_INVALID)) {
    tls_thread_koid = GetKoid(zx_thread_self());
  }
  ZX_DEBUG_ASSERT(tls_thread_koid != ZX_KOID_INVALID);
  return tls_thread_koid;
}

}  // namespace

void fx_logger::ActivateFallback(int fallback_fd) {
  fbl::AutoLock lock(&logger_mutex_);
  if (logger_fd_.load(std::memory_order_relaxed) != -1) {
    return;
  }
  ZX_DEBUG_ASSERT(fallback_fd >= -1);
  if (tagstr_.empty()) {
    for (size_t i = 0; i < tags_.size(); i++) {
      if (tagstr_.empty()) {
        tagstr_ = tags_[i];
      } else {
        tagstr_ = fbl::String::Concat({tagstr_, ", ", tags_[i]});
      }
    }
  }
  if (fallback_fd == -1) {
    fallback_fd = STDERR_FILENO;
  }
  // Do not change fd_to_close_ as we don't want to close fallback_fd.
  // We will still close original console_fd_
  logger_fd_.store(fallback_fd, std::memory_order_relaxed);
  socket_.reset();
}

zx_status_t fx_logger::Reconfigure(const fx_logger_config_t* config) {
  if (!config)
    return ZX_ERR_INVALID_ARGS;
  if (config->log_service_channel != ZX_HANDLE_INVALID || config->console_fd != -1) {
    socket_.reset(config->log_service_channel);
    fd_to_close_.reset(config->console_fd);
    logger_fd_.store(config->console_fd, std::memory_order_relaxed);
    // We don't expect to have a console fd and a socket at the same time.
    ZX_DEBUG_ASSERT(fd_to_close_ != socket_.is_valid());
  }

  SetSeverity(config->min_severity);
  return SetTags(config->tags, config->num_tags);
}

zx_status_t fx_logger::GetLogConnectionStatus() {
  auto has_socket = socket_.is_valid();
  auto has_fallback = logger_fd_.load(std::memory_order_relaxed) != -1;
  if (has_socket)
    return ZX_OK;
  if (has_fallback)
    return ZX_ERR_NOT_CONNECTED;
  return ZX_ERR_BAD_STATE;
}

void fx_logger::SetLogConnection(zx_handle_t handle) {
  fbl::AutoLock lock(&logger_mutex_);
  if (handle != ZX_HANDLE_INVALID) {
    socket_.reset(handle);
    fd_to_close_.reset(logger_fd_.load(std::memory_order_relaxed));
    logger_fd_.store(-1);
  }
}

zx_status_t fx_logger::VLogWriteToSocket(fx_log_severity_t severity, const char* tag,
                                         const char* msg, va_list args, bool perform_format) {
  zx_time_t time = zx_clock_get_monotonic();
  fx_log_packet_t packet;
  memset(&packet, 0, sizeof(packet));
  constexpr size_t kDataSize = sizeof(packet.data);
  packet.metadata.pid = pid_;
  packet.metadata.tid = GetCurrentThreadKoid();
  packet.metadata.time = time;
  packet.metadata.severity = severity;
  packet.metadata.dropped_logs = dropped_logs_.load();

  // Write tags
  size_t pos = 0;
  for (size_t i = 0; i < tags_.size(); i++) {
    size_t len = tags_[i].length();
    ZX_DEBUG_ASSERT(len < 128);
    packet.data[pos++] = static_cast<char>(len);
    memcpy(packet.data + pos, tags_[i].c_str(), len);
    pos += len;
  }
  if (tag != NULL) {
    size_t len = strlen(tag);
    if (len > 0) {
      size_t write_len = std::min(len, static_cast<size_t>(FX_LOG_MAX_TAG_LEN - 1));
      ZX_DEBUG_ASSERT(write_len < 128);
      packet.data[pos++] = static_cast<char>(write_len);
      memcpy(packet.data + pos, tag, write_len);
      pos += write_len;
    }
  }
  packet.data[pos++] = 0;
  ZX_DEBUG_ASSERT(pos < kDataSize);
  // Write msg
  int n = static_cast<int>(kDataSize - pos);
  int count = 0;
  size_t msg_pos = pos;
  if (!perform_format) {
    size_t write_len = std::min(strlen(msg), static_cast<size_t>(n - 1));
    memcpy(packet.data + pos, msg, write_len);
    pos += write_len;
    packet.data[pos] = 0;
    count = static_cast<int>(write_len + 1);
  } else {
    count = vsnprintf(packet.data + pos, n, msg, args);
    if (count < 0) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  if (count >= n) {
    // truncated
    constexpr char kEllipsis[] = "...";
    constexpr size_t kEllipsisSize = sizeof(kEllipsis) - 1;
    memcpy(packet.data + kDataSize - 1 - kEllipsisSize, kEllipsis, kEllipsisSize);
    count = n - 1;
  }
  auto size = sizeof(packet.metadata) + msg_pos + count + 1;
  ZX_DEBUG_ASSERT(size <= sizeof(packet));
  auto status = socket_.write(0, &packet, size, nullptr);
  if (status == ZX_ERR_BAD_STATE || status == ZX_ERR_PEER_CLOSED) {
    ActivateFallback(-1);
    return VLogWriteToFd(logger_fd_.load(std::memory_order_relaxed), severity, tag,
                         packet.data + msg_pos, args, false);
  }
  if (status != ZX_OK) {
    dropped_logs_.fetch_add(1);
  }
  return status;
}

zx_status_t fx_logger::VLogWriteToFd(int fd, fx_log_severity_t severity, const char* tag,
                                     const char* msg, va_list args, bool perform_format) {
  zx_time_t time = zx_clock_get_monotonic();
  constexpr char kEllipsis[] = "...";
  constexpr size_t kEllipsisSize = sizeof(kEllipsis) - 1;
  constexpr size_t kMaxMessageSize = FX_LOG_MAX_DATAGRAM_LEN;

  fbl::StringBuffer<kMaxMessageSize + kEllipsisSize + 1 /*\n*/> buf;
  buf.AppendPrintf("[%05ld.%06ld]", time / 1000000000UL, (time / 1000UL) % 1000000UL);
  buf.AppendPrintf("[%ld]", pid_);
  buf.AppendPrintf("[%ld]", GetCurrentThreadKoid());

  buf.Append("[");
  if (!tagstr_.empty()) {
    buf.Append(tagstr_);
  }

  if (tag != NULL) {
    size_t len = strlen(tag);
    if (len > 0) {
      if (!tagstr_.empty()) {
        buf.Append(", ");
      }
      buf.Append(tag, std::min(len, static_cast<size_t>(FX_LOG_MAX_TAG_LEN - 1)));
    }
  }
  buf.Append("]");
  switch (severity) {
    case FX_LOG_TRACE:
      buf.Append(" TRACE");
      break;
    case FX_LOG_DEBUG:
      buf.Append(" DEBUG");
      break;
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
      buf.AppendPrintf(" VLOG(%d)", (FX_LOG_INFO - severity));
  }
  buf.Append(": ");

  if (!perform_format) {
    buf.Append(msg);
  } else {
    buf.AppendVPrintf(msg, args);
  }
  if (buf.size() > kMaxMessageSize) {
    buf.Resize(kMaxMessageSize);
    buf.Append(kEllipsis);
  }
  buf.Append('\n');
  ssize_t status = write(fd, buf.data(), buf.size());
  if (status < 0) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t fx_logger::VLogWrite(fx_log_severity_t severity, const char* tag, const char* msg,
                                 va_list args, bool perform_format) {
  if (msg == NULL || severity > (FX_LOG_SEVERITY_MAX * FX_LOG_SEVERITY_STEP_SIZE)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (GetSeverity() > severity) {
    return ZX_OK;
  }

  zx_status_t status;
  int fd = logger_fd_.load(std::memory_order_relaxed);
  if (fd != -1) {
    status = VLogWriteToFd(fd, severity, tag, msg, args, perform_format);
  } else if (socket_.is_valid()) {
    status = VLogWriteToSocket(severity, tag, msg, args, perform_format);
  } else {
    return ZX_ERR_BAD_STATE;
  }
  if (severity == FX_LOG_FATAL) {
    abort();
  }
  return status;
}

// This function is not thread safe
zx_status_t fx_logger::SetTags(const char** tags, size_t ntags) {
  if (ntags > FX_LOG_MAX_TAGS) {
    return ZX_ERR_INVALID_ARGS;
  }

  tags_.reset();
  tagstr_ = "";

  auto fd_mode = logger_fd_.load(std::memory_order_relaxed) != -1;
  for (size_t i = 0; i < ntags; i++) {
    auto len = strlen(tags[i]);
    fbl::String str(tags[i], len > FX_LOG_MAX_TAG_LEN - 1 ? FX_LOG_MAX_TAG_LEN - 1 : len);
    if (fd_mode) {
      if (tagstr_.empty()) {
        tagstr_ = str;
      } else {
        tagstr_ = fbl::String::Concat({tagstr_, ", ", str});
      }
    } else {
      tags_.push_back(str);
    }
  }
  return ZX_OK;
}
