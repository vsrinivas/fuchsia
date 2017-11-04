// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zx/process.h>
#include <zx/socket.h>
#include <zx/thread.h>

#include "fx_logger.h"

namespace syslog {
namespace {

// This thread's koid.
// Initialized on first use.
thread_local zx_koid_t tls_thread_koid{ZX_KOID_INVALID};

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_koid_t GetCurrentProcessKoid() {
  auto koid = GetKoid(zx::process::self().get());
  ZX_DEBUG_ASSERT(koid != ZX_KOID_INVALID);
  return koid;
}

zx_koid_t GetCurrentThreadKoid() {
  if (unlikely(tls_thread_koid == ZX_KOID_INVALID)) {
    tls_thread_koid = GetKoid(zx::thread::self().get());
  }
  ZX_DEBUG_ASSERT(tls_thread_koid != ZX_KOID_INVALID);
  return tls_thread_koid;
}
}  // namespace
}  // namespace syslog

struct fx_logger {
 public:
  // If tags or ntags are out of bound, this constructor will not fail but it
  // will not store all the tags and global tag behaviour would be undefined.
  // So they should be validated before calling this constructor.
  fx_logger(const fx_logger_config_t* config) {
    pid_ = syslog::GetCurrentProcessKoid();
    // TODO: set socket if available
    console_fd_.reset(config->console_fd);
    SetSeverity(config->min_severity);
    AddTags(config->tags, config->num_tags);
  }

  ~fx_logger() = default;

  zx_status_t VLogWrite(fx_log_severity_t severity, const char* tag,
                        const char* format, va_list args);

  void SetSeverity(fx_log_severity_t log_severity) {
    severity_.store(log_severity, fbl::memory_order_relaxed);
  }

  fx_log_severity_t GetSeverity() {
    return severity_.load(fbl::memory_order_relaxed);
  }

 private:
  zx_status_t AddTags(const char** tags, size_t ntags);

  zx_koid_t pid_;
  fbl::atomic<fx_log_severity_t> severity_;
  fbl::unique_fd console_fd_;
  zx::socket socket_;
  fbl::Vector<fbl::String> tags_;

  // string representation to print in fallback mode
  fbl::String tagstr_;
};
