// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>

#include <syslog/logger.h>

#include "fx_logger.h"

namespace {

// This thread's koid.
// Initialized on first use.
thread_local zx_koid_t tls_thread_koid{ZX_KOID_INVALID};

zx_koid_t GetCurrentThreadKoid() {
  if (unlikely(tls_thread_koid == ZX_KOID_INVALID)) {
    tls_thread_koid = GetKoid(zx::thread::self().get());
  }
  ZX_DEBUG_ASSERT(tls_thread_koid != ZX_KOID_INVALID);
  return tls_thread_koid;
}

}  // namespace

zx_status_t fx_logger::VLogWrite(fx_log_severity_t severity, const char* tag,
                                 const char* msg, va_list args) {
  if (msg == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (GetSeverity() > severity) {
    return ZX_OK;
  }

  // return until we add socket support
  if (console_fd_.get() == -1) {
    return ZX_OK;
  }

  zx_time_t time = zx_clock_get(ZX_CLOCK_MONOTONIC);
  constexpr char kEllipsis[] = "...";
  constexpr size_t kEllipsisSize = sizeof(kEllipsis) - 1;
  constexpr size_t kMaxMessageSize = 2043;

  fbl::StringBuffer<kMaxMessageSize + kEllipsisSize + 1 /*\n*/> buf;
  buf.AppendPrintf("[%ld]", time);
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
      buf.Append(tag, fbl::min(len, static_cast<size_t>(FX_LOG_MAX_TAG_LEN)));
    }
  }
  buf.Append("]");
  switch (severity) {
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
      buf.AppendPrintf(" VLOG(%d)", -severity);
  }
  buf.Append(": ");

  buf.AppendVPrintf(msg, args);
  if (buf.size() > kMaxMessageSize) {
    buf.Resize(kMaxMessageSize);
    buf.Append(kEllipsis);
  }
  buf.Append('\n');
  ssize_t status = write(console_fd_.get(), buf.data(), buf.size());
  if (status < 0) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

// This function is not thread safe
zx_status_t fx_logger::AddTags(const char** tags, size_t ntags) {
  if (ntags > FX_LOG_MAX_TAGS) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto fd_mode = console_fd_.get() != -1;
  for (size_t i = 0; i < ntags; i++) {
    auto len = strlen(tags[i]);
    fbl::String str(tags[i],
                    len > FX_LOG_MAX_TAG_LEN ? FX_LOG_MAX_TAG_LEN : len);
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
