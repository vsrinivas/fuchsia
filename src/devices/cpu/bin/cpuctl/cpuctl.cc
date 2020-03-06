// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/hardware/cpu/ctrl/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/types.h>

#include <fbl/auto_call.h>

namespace cpuctrl = llcpp::fuchsia::hardware::cpu::ctrl;
namespace fuchsia_device = llcpp::fuchsia::device;

using llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES;

using ListCb = void (*)(const char* path);

constexpr char kCpuDevicePath[] = "/dev/class/cpu-ctrl";
constexpr char kCpuDeviceFormat[] = "/dev/class/cpu-ctrl/%s";
constexpr size_t kMaxPathLen = 24;  // strlen("/dev/class/cpu-ctrl/000\0")

// Print help message to stderr.
void usage(const char* cmd) {
  // Purely aesthetic, but create a buffer of space characters so multiline subtask
  // descriptions can be justified with the preceeding line.
  const size_t kCmdLen = strlen(cmd) + 1;
  const auto spaces_cleanup = std::make_unique<char[]>(kCmdLen);
  char* spaces = spaces_cleanup.get();
  memset(spaces, ' ', kCmdLen);  // Fill buffer with spaces.
  spaces[kCmdLen - 1] = '\0';    // Null terminate.

  fprintf(stderr, "\nInteract with the CPU\n");
  fprintf(stderr, "\t%s help                   Print this message and quit.\n", cmd);
  fprintf(stderr, "\t%s list                   List this system's performance domains\n", cmd);
  fprintf(stderr, "\t%s describe [domain]      Describes a given performance domain's performance states\n",
          cmd);
  fprintf(stderr, "\t%s                        describes all domains if `domain` is omitted.\n",
          spaces);
  fprintf(stderr, "\t%s pstate domain state    Set the CPU's performance state.\n", cmd);
}

// Call `ListCb cb` with all the names of devices in kCpuDevicePath. Each of
// these devices represent a single performance domain.
zx_status_t list(ListCb cb) {
  DIR* dir = opendir(kCpuDevicePath);
  if (!dir) {
    fprintf(stderr, "Failed to open CPU device at '%s'\n", kCpuDevicePath);
    return -1;
  }

  auto cleanup = fbl::MakeAutoCall([&dir]() { closedir(dir); });

  struct dirent* de = nullptr;
  while ((de = readdir(dir)) != nullptr) {
    cb(de->d_name);
  }
  return ZX_OK;
}

// Print each performance domain to stdout.
void print_performance_domain(const char* path) {
  // Paths take the form /dev/class/cpu/NNN so we expect the
  // path to be exactly 3 characters long.
  if (strnlen(path, 4) == 3) {
    printf("Domain %s\n", path);
  } else {
    // Why isn't the path 3 characters?
    printf("Domain ???\n");
  }
}

void describe(const char* domain) {
  char path[kMaxPathLen];
  snprintf(path, kMaxPathLen, kCpuDeviceFormat, domain);

  zx::channel channel_local, channel_remote;
  zx_status_t st = zx::channel::create(0, &channel_local, &channel_remote);
  if (st != ZX_OK) {
    fprintf(stderr, "Failed to create channel pair, st = %d\n", st);
    return;
  }

  st = fdio_service_connect(path, channel_remote.release());
  if (st != ZX_OK) {
    fprintf(stderr, "Failed to connect to service at '%s', st = %d\n", path, st);
    return;
  }

  auto client = std::make_unique<cpuctrl::Device::SyncClient>(std::move(channel_local));

  printf("Domain %s\n", domain);

  auto resp = client->GetNumLogicalCores();
  if (resp.status() != ZX_OK) {
    fprintf(stderr, "Failed to get num logical cores domain '%s'\n", domain);
  } else {
    printf("  Num Logical Cores: %lu\n", resp.value().count);
  }

  for (uint32_t i = 0; i < MAX_DEVICE_PERFORMANCE_STATES; i++) {
    auto resp = client->GetPerformanceStateInfo(i);

    if (resp.status() != ZX_OK) {
      fprintf(stderr, "Failed to get performance state %d for domain '%s'\n", i, domain);
      continue;
    }

    if (resp->result.is_err()) {
      // Maybe this performance state is not supported for this performance domain?
      // Skip silently.
      continue;
    }

    printf("  PState %d:\n", i);
    if (resp.value().result.response().info.frequency_hz == cpuctrl::FREQUENCY_UNKNOWN) {
      printf("    Freq (hz): unknown\n");
    } else {
      printf("    Freq (hz): %lu\n", resp.value().result.response().info.frequency_hz);
    }
    if (resp.value().result.response().info.voltage_uv == cpuctrl::VOLTAGE_UNKNOWN) {
      printf("    Volt (uv): unknown\n");
    } else {
      printf("    Volt (uv): %lu\n", resp.value().result.response().info.voltage_uv);
    }
  }
}

void set_performance_state(const char* domain, const char* pstate) {
  if (strnlen(domain, 4) != 3) {
    fprintf(stderr, "Domain must be 3 characters long (nnn)\n");
    return;
  }
  char path[kMaxPathLen];
  snprintf(path, kMaxPathLen, kCpuDeviceFormat, domain);

  char* end;
  long desired_state = strtol(pstate, &end, 10);
  if (end == pstate || *end != '\0'  ||
      desired_state < 0 || desired_state > MAX_DEVICE_PERFORMANCE_STATES) {
    fprintf(stderr, "Bad pstate '%s', must be a positive integer between 0 and %u\n", pstate,
                    MAX_DEVICE_PERFORMANCE_STATES);
    return;
  }

  zx::channel channel_local, channel_remote;
  zx_status_t st = zx::channel::create(0, &channel_local, &channel_remote);
  if (st != ZX_OK) {
    fprintf(stderr, "Failed to create channel pair, st = %d\n", st);
    return;
  }

  st = fdio_service_connect(path, channel_remote.release());
  if (st != ZX_OK) {
    fprintf(stderr, "Failed to connect to service at '%s', st = %d\n", path, st);
    return;
  }

  auto client = std::make_unique<fuchsia_device::Controller::SyncClient>(std::move(channel_local));

  auto result = client->SetPerformanceState(static_cast<uint32_t>(desired_state));
  if (!result.ok()) {
    fprintf(stderr, "Failed to set pstate\n");
    return;
  }

  if (result.status() != ZX_OK) {
    fprintf(stderr, "Failed to set pstate, st = %d\n", result.status());
    return;
  }

  printf("Set pstate for domain '%s' to %u\n", domain, result.value().out_state);
}

zx_status_t describe_all() {
  list(describe);
  return ZX_OK;
}

int main(int argc, char* argv[]) {
  const char* cmd = argv[0];
  if (argc == 1) {
    usage(argv[0]);
    return -1;
  }

  const char* subcmd = argv[1];

  if (!strncmp(subcmd, "help", 4)) {
    usage(cmd);
    return 0;
  } else if (!strncmp(subcmd, "list", 4)) {
    return list(print_performance_domain) == ZX_OK ? 0 : -1;
  } else if (!strncmp(subcmd, "describe", 8)) {
    if (argc >= 3) {
      describe(argv[2]);
      return 0;
    } else {
      return describe_all() == ZX_OK ? 0 : -1;
    }
  } else if (!strncmp(subcmd, "pstate", 6)) {
    if (argc >= 4) {
      set_performance_state(argv[2], argv[3]);
    } else {
      fprintf(stderr, "pstate <domain> <pstate>\n");
      usage(cmd);
      return -1;
    }
  }

  return 0;
}
