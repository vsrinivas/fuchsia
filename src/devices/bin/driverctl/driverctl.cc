// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>

static void usage(void) {
  fprintf(stderr,
          "Usage: driverctl <path> <command> [options]\n"
          "\n"
          "Where path is path to driver file in /dev\n"
          "\n"
          "Command \"log\":\n"
          "  Option is one of:\n"
          "    \"error\" or \"e\":   DDK_LOG_ERROR\n"
          "    \"warning\" or \"w\": DDK_LOG_WARNING\n"
          "    \"info\" or \"i\":    DDK_LOG_INFO\n"
          "    \"debug\" or \"d\":   DDK_LOG_DEBUG\n"
          "    \"trace\" or \"t\":   DDK_LOG_TRACE\n"
          "    \"serial\" or \"s\":  DDK_LOG_SERIAL\n"
          "\n"
          "  With no options provided, \"driverctl log\" will print the current\n"
          "  minimum log severity for the driver\n"
          "\n"
          "  For example, to set the minimum log severity to DDK_LOG_ERROR:\n"
          "    $ driverctl <path> log error\n"
          "  Or:\n"
          "    $ driverctl <path> log e\n");
}

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    usage();
    return -1;
  }

  const char* path = argv[1];
  if (!strcmp(path, "-h")) {
    usage();
    return 0;
  }

  const char* command = argv[2];
  if (strcmp(command, "log")) {
    fprintf(stderr, "Unsupported command \"%s\"\n", command);
    usage();
    return -1;
  }

  zx::channel device, device_remote;
  if (zx::channel::create(0, &device, &device_remote) != ZX_OK) {
    fprintf(stderr, "Failed to create channel\n");
    return -1;
  }
  zx_status_t status = fdio_service_connect(path, device_remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to open %s\n", path);
    return -1;
  }

  if (argc == 3) {
    auto response = fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(device))
                        ->GetMinDriverLogSeverity();
    if (response.status() != ZX_OK) {
      fprintf(stderr, "Failed to send GetMinDriverLogSeverity request: %s\n",
              zx_status_get_string(response.status()));
      return -1;
    }
    if (response.Unwrap_NEW()->status != ZX_OK) {
      fprintf(stderr, "GetMinDriverLogSeverity returned an error: %s\n",
              zx_status_get_string(response->status));
      return -1;
    }
    printf("Log severity: ");
    switch (static_cast<fx_log_severity_t>(response.Unwrap_NEW()->severity)) {
      case DDK_LOG_ERROR:
        printf("error\n");
        break;
      case DDK_LOG_WARNING:
        printf("warning\n");
        break;
      case DDK_LOG_INFO:
        printf("info\n");
        break;
      case DDK_LOG_DEBUG:
        printf("debug\n");
        break;
      case DDK_LOG_TRACE:
        printf("trace\n");
        break;
      case DDK_LOG_SERIAL:
        printf("serial\n");
        break;
      default:
        printf("unknown\n");
        break;
    }
    return 0;
  }

  const char* arg = argv[3];
  fx_log_severity_t flags = 0;
  if (!strcasecmp(arg, "e") || !strcasecmp(arg, "error")) {
    flags = DDK_LOG_ERROR;
  } else if (!strcasecmp(arg, "w") || !strcasecmp(arg, "warning")) {
    flags = DDK_LOG_WARNING;
  } else if (!strcasecmp(arg, "i") || !strcasecmp(arg, "info")) {
    flags = DDK_LOG_INFO;
  } else if (!strcasecmp(arg, "d") || !strcasecmp(arg, "debug")) {
    flags = DDK_LOG_DEBUG;
  } else if (!strcasecmp(arg, "t") || !strcasecmp(arg, "trace")) {
    flags = DDK_LOG_TRACE;
  } else if (!strcasecmp(arg, "s") || !strcasecmp(arg, "serial")) {
    flags = DDK_LOG_SERIAL;
  } else {
    fprintf(stderr, "Unknown log severity \"%s\"\n", arg);
    return -1;
  }
  auto response = fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(device))
                      ->SetMinDriverLogSeverity(flags);
  if (response.status() != ZX_OK || response.Unwrap_NEW()->status != ZX_OK) {
    fprintf(stderr, "SetDriverLogFlags failed for %s\n", path);
  }
  return 0;
}
