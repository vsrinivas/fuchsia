// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <ddk/debug.h>

static void usage(void) {
  fprintf(
      stderr,
      "Usage: driverctl <path> <command> [options]\n"
      "\n"
      "where path is path to driver file in /dev\n"
      "\n"
      "Command \"log\":\n"
      "  options are zero or more of:\n"
      "    \"error\" or \"e\":   DDK_LOG_ERROR\n"
      "    \"warn\" or \"w\":    DDK_LOG_WARN\n"
      "    \"info\" or \"i\":    DDK_LOG_INFO\n"
      "    \"trace\" or \"t\":   DDK_LOG_TRACE\n"
      "    \"spew\" or \"s\":    DDK_LOG_SPEW\n"
      "\n"
      "  With no options provided, driverctl log will print the current log flags for the driver.\n"
      "  A flag may have a '+' or '-' prepended. In that case the flag will be toggled\n"
      "  on (+) or off(-) without affecting other flags.\n"
      "  If toggled flags are used, all flags must be toggled.\n"
      "\n"
      "  Examples:\n"
      "\n"
      "  Set log flags to DDK_LOG_ERROR | DDK_LOG_INFO | DDK_LOG_TRACE:\n"
      "    $ driverctl <path> log error info trace\n"
      "  or:\n"
      "    $ driverctl <path> log e i t\n"
      "\n"
      "  Turn on DDK_LOG_TRACE and DDK_LOG_SPEW:\n"
      "    $ driverctl <path> log +trace +spew\n"
      "  or:\n"
      "    $ driverctl <path> log +t +s\n"
      "\n"
      "  Turn off DDK_LOG_SPEW:\n"
      "    $ driverctl <path> log -spew\n"
      "  or:\n"
      "    $ driverctl <path> log -s\n");
}

int main(int argc, char** argv) {
  int ret = 0;

  if (argc < 3) {
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
    fprintf(stderr, "Unsupported command %s\n", command);
    usage();
    return -1;
  }

  zx::channel device, device_remote;
  if (zx::channel::create(0, &device, &device_remote) != ZX_OK) {
    fprintf(stderr, "could not create channel\n");
    return -1;
  }
  zx_status_t status = fdio_service_connect(path, device_remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "could not open %s\n", path);
    return -1;
  }

  if (argc == 3) {
    uint32_t flags;
    zx_status_t call_status;
    auto resp =
        ::llcpp::fuchsia::device::Controller::Call::GetDriverLogFlags(zx::unowned_channel(device));
    status = resp.status();
    call_status = resp->status;
    flags = resp->flags;
    if (status != ZX_OK || call_status != ZX_OK) {
      fprintf(stderr, "GetDriverLogFlags failed for %s\n", path);
    } else {
      printf("Log flags:");
      if (flags & DDK_LOG_ERROR) {
        printf(" ERROR");
      }
      if (flags & DDK_LOG_WARN) {
        printf(" WARN");
      }
      if (flags & DDK_LOG_INFO) {
        printf(" INFO");
      }
      if (flags & DDK_LOG_TRACE) {
        printf(" TRACE");
      }
      if (flags & DDK_LOG_SPEW) {
        printf(" SPEW");
      }
      printf("\n");
    }
    return ret;
  }

  uint32_t clear_flags = 0;
  uint32_t set_flags = 0;
  char* toggle_arg = NULL;
  char* non_toggle_arg = NULL;

  for (int i = 3; i < argc; i++) {
    char* arg = argv[i];
    char toggle = arg[0];
    uint32_t flag = 0;

    // check for leading + or -
    if (toggle == '+' || toggle == '-') {
      toggle_arg = arg;
      arg++;
    } else {
      non_toggle_arg = arg;
    }

    if (toggle_arg && non_toggle_arg) {
      fprintf(stderr, "Cannot mix toggled flag \"%s\" with non-toggle flag \"%s\"\n", toggle_arg,
              non_toggle_arg);
      usage();
      ret = -1;
      return ret;
    }

    if (!strcasecmp(arg, "e") || !strcasecmp(arg, "error")) {
      flag = DDK_LOG_ERROR;
    } else if (!strcasecmp(arg, "w") || !strcasecmp(arg, "warn")) {
      flag = DDK_LOG_WARN;
    } else if (!strcasecmp(arg, "i") || !strcasecmp(arg, "info")) {
      flag = DDK_LOG_INFO;
    } else if (!strcasecmp(arg, "t") || !strcasecmp(arg, "trace")) {
      flag = DDK_LOG_TRACE;
    } else if (!strcasecmp(arg, "s") || !strcasecmp(arg, "spew")) {
      flag = DDK_LOG_SPEW;
    } else {
      fprintf(stderr, "unknown flag %s\n", arg);
      ret = -1;
      return ret;
    }

    if (toggle == '+') {
      set_flags |= flag;
    } else if (toggle == '-') {
      clear_flags |= flag;
    } else {
      set_flags |= flag;
    }
  }

  if (!toggle_arg) {
    // clear all flags not explicitly set if we aren't using flag toggles
    clear_flags = ~set_flags;
  }

  zx_status_t call_status;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::SetDriverLogFlags(
      zx::unowned_channel(device), clear_flags, set_flags);
  status = resp.status();
  call_status = resp->status;
  if (status != ZX_OK || call_status != ZX_OK) {
    fprintf(stderr, "SetDriverLogFlags failed for %s\n", path);
  }
}
