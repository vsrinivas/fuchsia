// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/syslog/structured_backend/cpp/fuchsia_syslog.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <stdarg.h>
#include <stdio.h>

#include "platform_logger.h"
#include "platform_thread.h"
#include "zircon_platform_handle.h"

namespace magma {

namespace {

bool g_is_logging_initialized = false;
// Intentionally leaked on shutdown to ensure there are no destructor ordering problems.
zx_handle_t log_socket;

}  // namespace

bool PlatformLogger::IsInitialized() { return g_is_logging_initialized; }

bool PlatformLogger::Initialize(std::unique_ptr<PlatformHandle> channel) {
  zx::socket local_socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &local_socket, &remote_socket);
  if (status != ZX_OK)
    return false;

  auto zircon_handle = static_cast<ZirconPlatformHandle*>(channel.get());

  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_logger::LogSink>(
                                   zx::unowned_channel(zircon_handle->get())))
                    ->ConnectStructured(std::move(remote_socket));
  if (result.status() != ZX_OK)
    return false;

  log_socket = local_socket.release();

  g_is_logging_initialized = true;
  return true;
}

static FuchsiaLogSeverity get_severity(PlatformLogger::LogLevel level) {
  switch (level) {
    case PlatformLogger::LOG_INFO:
      return FUCHSIA_LOG_INFO;
    case PlatformLogger::LOG_WARNING:
      return FUCHSIA_LOG_WARNING;
    case PlatformLogger::LOG_ERROR:
      return FUCHSIA_LOG_ERROR;
  }
}

static const char* StripPath(const char* path) {
  auto p = strrchr(path, '/');
  if (p) {
    return p + 1;
  } else {
    return path;
  }
}

void PlatformLogger::LogVa(LogLevel level, const char* file, int line, const char* msg,
                           va_list args) {
  if (log_socket == ZX_HANDLE_INVALID) {
    return;
  }
  constexpr size_t kFormatStringLength = 1024;
  char fmt_string[kFormatStringLength];
  fmt_string[kFormatStringLength - 1] = 0;
  int n = kFormatStringLength;
  // Format
  // Number of bytes written not including null terminator
  int count = vsnprintf(fmt_string, n, msg, args);
  if (count < 0) {
    return;
  }

  // Add null terminator.
  count++;

  if (count >= n) {
    // truncated
    constexpr char kEllipsis[] = "...";
    constexpr size_t kEllipsisSize = sizeof(kEllipsis);
    snprintf(fmt_string + kFormatStringLength - 1 - kEllipsisSize, kEllipsisSize, kEllipsis);
  }

  std::string file_string = StripPath(file);
  fuchsia_syslog::LogBuffer log_buffer;
  uint64_t tid = PlatformThreadId().id();
  uint64_t pid = PlatformProcessHelper::GetCurrentProcessId();
  log_buffer.BeginRecord(get_severity(level), file, line, fmt_string, /*condition*/ {},
                         /*is_printf*/ false, zx::unowned_socket(log_socket), 0, pid, tid);
  log_buffer.WriteKeyValue("tag", "magma");
  log_buffer.FlushRecord();
}

}  // namespace magma
