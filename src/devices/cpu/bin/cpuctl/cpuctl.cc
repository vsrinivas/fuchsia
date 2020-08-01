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

#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fbl/auto_call.h>

#include "performance-domain.h"

using llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES;
using ListCb = void (*)(const char* path);

constexpr char kCpuDevicePath[] = "/dev/class/cpu-ctrl";
constexpr char kCpuDeviceFormat[] = "/dev/class/cpu-ctrl/%s";
constexpr size_t kMaxPathLen = 24;  // strlen("/dev/class/cpu-ctrl/000\0")

void print_frequency(const cpuctrl::CpuPerformanceStateInfo& info) {
  if (info.frequency_hz == cpuctrl::FREQUENCY_UNKNOWN) {
    std::cout << "(unknown)";
  } else {
    std::cout << info.frequency_hz << "hz";
  }
}

void print_voltage(const cpuctrl::CpuPerformanceStateInfo& info) {
  if (info.voltage_uv == cpuctrl::VOLTAGE_UNKNOWN) {
    std::cout << "(unknown)";
  } else {
    std::cout << info.voltage_uv << "uv";
  }
}

// Print help message to stderr.
void usage(const char* cmd) {
  // Purely aesthetic, but create a buffer of space characters so multiline subtask
  // descriptions can be justified with the preceeding line.
  const size_t kCmdLen = strlen(cmd) + 1;
  const auto spaces_cleanup = std::make_unique<char[]>(kCmdLen);
  char* spaces = spaces_cleanup.get();
  memset(spaces, ' ', kCmdLen);  // Fill buffer with spaces.
  spaces[kCmdLen - 1] = '\0';    // Null terminate.

  // clang-format off
  fprintf(stderr, "\nInteract with the CPU\n");
  fprintf(stderr, "\t%s help                     Print this message and quit.\n", cmd);
  fprintf(stderr, "\t%s list                     List this system's performance domains\n", cmd);
  fprintf(stderr, "\t%s describe [domain]        Describes a given performance domain's performance states\n",
          cmd);
  fprintf(stderr, "\t%s                          describes all domains if `domain` is omitted.\n",
          spaces);
  
  fprintf(stderr, "\t%s pstate <domain> [state]  Set the CPU's performance state to `state`. \n",
          cmd);
  fprintf(stderr, "\t%s                          Returns the current state if `state` is omitted.\n",
          spaces);
  // clang-format on
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

void describe(const char* domain_name) {
  char path[kMaxPathLen];
  snprintf(path, kMaxPathLen, kCpuDeviceFormat, domain_name);

  auto domain = CpuPerformanceDomain::CreateFromPath(path);

  // If status is set, something went wront.
  zx_status_t* st = std::get_if<zx_status_t>(&domain);
  if (st) {
    std::cerr << "Failed to connect to performance domain device '" << domain_name << "'"
              << " st = " << *st << std::endl;
    return;
  }

  auto client = std::move(std::get<CpuPerformanceDomain>(domain));
  const auto& [core_count_status, core_count] = client.GetNumLogicalCores();

  std::cout << "Domain " << domain_name << std::endl;
  if (core_count_status == ZX_OK) {
    std::cout << core_count << " logical cores" << std::endl;
  }

  const auto& pstates = client.GetPerformanceStates();
  for (size_t i = 0; i < pstates.size(); i++) {
    std::cout << " + pstate: " << i << std::endl;

    std::cout << "   - freq: ";
    print_frequency(pstates[i]);
    std::cout << std::endl;

    std::cout << "   - volt: ";
    print_voltage(pstates[i]);
    std::cout << std::endl;
  }
}

void set_performance_state(const char* domain_name, const char* pstate) {
  char* end;
  long desired_state_l = strtol(pstate, &end, 10);
  if (end == pstate || *end != '\0' || desired_state_l < 0 ||
      desired_state_l > MAX_DEVICE_PERFORMANCE_STATES) {
    fprintf(stderr, "Bad pstate '%s', must be a positive integer between 0 and %u\n", pstate,
            MAX_DEVICE_PERFORMANCE_STATES);
    return;
  }

  uint32_t desired_state = static_cast<uint32_t>(desired_state_l);
  char path[kMaxPathLen];
  snprintf(path, kMaxPathLen, kCpuDeviceFormat, domain_name);

  auto domain = CpuPerformanceDomain::CreateFromPath(path);

  zx_status_t* st = std::get_if<zx_status_t>(&domain);
  if (st) {
    std::cerr << "Failed to connect to performance domain device '" << domain_name << "'"
              << " st = " << *st << std::endl;
    return;
  }

  auto client = std::move(std::get<CpuPerformanceDomain>(domain));

  zx_status_t status = client.SetPerformanceState(static_cast<uint32_t>(desired_state));
  if (status != ZX_OK) {
    std::cerr << "Failed to set performance state, st = " << status << std::endl;
    return;
  }

  std::cout << "PD: " << domain_name << " set pstate to " << desired_state << std::endl;

  const auto& pstates = client.GetPerformanceStates();
  if (desired_state < pstates.size()) {
    std::cout << "freq: ";
    print_frequency(pstates[desired_state]);
    std::cout << " ";

    std::cout << "volt: ";
    print_voltage(pstates[desired_state]);
    std::cout << std::endl;
  }
}

void get_performance_state(const char* domain_name) {
  char path[kMaxPathLen];
  snprintf(path, kMaxPathLen, kCpuDeviceFormat, domain_name);

  auto domain = CpuPerformanceDomain::CreateFromPath(path);

  // If status is set, something went wront.
  zx_status_t* st = std::get_if<zx_status_t>(&domain);
  if (st) {
    std::cerr << "Failed to connect to performance domain device '" << domain_name << "'"
              << " st = " << *st << std::endl;
    return;
  }

  auto client = std::move(std::get<CpuPerformanceDomain>(domain));

  const auto& [status, ps_index, pstate] = client.GetCurrentPerformanceState();

  if (status != ZX_OK) {
    std::cout << "Failed to get current performance state, st = " << status << std::endl;
    return;
  }

  std::cout << "Current Pstate = " << ps_index << std::endl;
  std::cout << "  Frequency: ";
  print_frequency(pstate);
  std::cout << std::endl;
  std::cout << "    Voltage: ";
  print_voltage(pstate);
  std::cout << std::endl;
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
    if (argc == 4) {
      set_performance_state(argv[2], argv[3]);
    } else if (argc == 3) {
      get_performance_state(argv[2]);
    } else {
      fprintf(stderr, "pstate <domain> [pstate]\n");
      usage(cmd);
      return -1;
    }
  } else if (!strncmp(subcmd, "stress", 6)) {
  }

  return 0;
}
