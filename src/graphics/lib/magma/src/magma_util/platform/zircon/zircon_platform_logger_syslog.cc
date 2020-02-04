// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/logger/llcpp/fidl.h>
#include <lib/syslog/global.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <stdarg.h>
#include <stdio.h>

#include "platform_logger.h"
#include "zircon_platform_handle.h"

namespace magma {

bool PlatformLogger::IsInitialized() { return fx_log_get_logger() != nullptr; }

bool PlatformLogger::Initialize(std::unique_ptr<PlatformHandle> handle) {
  zx::socket local_socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &local_socket, &remote_socket);
  if (status != ZX_OK)
    return false;

  auto zircon_handle = static_cast<ZirconPlatformHandle*>(handle.get());

  auto result = llcpp::fuchsia::logger::LogSink::Call::Connect(
      zx::unowned_channel(zircon_handle->get()), std::move(remote_socket));
  if (result.status() != ZX_OK)
    return false;

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = local_socket.release(),
                               .tags = nullptr,
                               .num_tags = 0};

  status = fx_log_init_with_config(&config);
  if (status != ZX_OK)
    return false;

  return true;
}

static fx_log_severity_t get_severity(PlatformLogger::LogLevel level) {
  switch (level) {
    case PlatformLogger::LOG_INFO:
      return FX_LOG_INFO;
    case PlatformLogger::LOG_WARNING:
      return FX_LOG_WARNING;
    case PlatformLogger::LOG_ERROR:
      return FX_LOG_ERROR;
  }
}

void PlatformLogger::LogVa(LogLevel level, const char* msg, va_list args) {
  _FX_LOGVF(get_severity(level), "magma", msg, args);
}

}  // namespace magma
