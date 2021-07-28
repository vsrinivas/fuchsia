// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cpuid.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/system.h>

static zx_handle_t root_resource;

// degrees Celsius below threshold before we adjust PL value
constexpr float kCoolThresholdCelsius = 5.0f;

class PlatformConfiguration {
 public:
  static std::unique_ptr<PlatformConfiguration> Create();

  zx_status_t SetMinPL1() { return SetPL1Mw(pl1_min_mw_); }
  zx_status_t SetMaxPL1() { return SetPL1Mw(pl1_max_mw_); }

  bool IsAtMax() { return current_pl1_mw_ == pl1_max_mw_; }
  bool IsAtMin() { return current_pl1_mw_ == pl1_min_mw_; }

 private:
  PlatformConfiguration(uint32_t pl1_min_mw, uint32_t pl1_max_mw)
      : pl1_min_mw_(pl1_min_mw), pl1_max_mw_(pl1_max_mw) {}

  zx_status_t SetPL1Mw(uint32_t target_mw);

  const uint32_t pl1_min_mw_;
  const uint32_t pl1_max_mw_;

  static constexpr uint32_t kEvePL1MinMw = 2500;
  static constexpr uint32_t kEvePL1MaxMw = 7000;

  static constexpr uint32_t kAtlasPL1MinMw = 3000;
  static constexpr uint32_t kAtlasPL1MaxMw = 7000;

  uint32_t current_pl1_mw_;
};

static zx_status_t get_root_resource(zx_handle_t* root_resource) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect("/svc/fuchsia.boot.RootResource", remote.release());
  if (status != ZX_OK) {
    return ZX_ERR_NOT_FOUND;
  }

  return fuchsia_boot_RootResourceGet(local.get(), root_resource);
}

std::unique_ptr<PlatformConfiguration> PlatformConfiguration::Create() {
  unsigned int a, b, c, d;
  unsigned int leaf_num = 0x80000002;
  char brand_string[50];
  memset(brand_string, 0, sizeof(brand_string));
  for (int i = 0; i < 3; i++) {
    if (!__get_cpuid(leaf_num + i, &a, &b, &c, &d)) {
      return nullptr;
    }
    memcpy(brand_string + (i * 16), &a, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 4, &b, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 8, &c, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 12, &d, sizeof(uint32_t));
  }
  // Only run thermd for processors used in Pixelbooks. The PL1 min/max settings are specified by
  // the chipset.
  if (strstr(brand_string, "i5-7Y57") || strstr(brand_string, "i7-7Y75")) {
    return std::unique_ptr<PlatformConfiguration>(
        new PlatformConfiguration(kEvePL1MinMw, kEvePL1MaxMw));
  } else if (strstr(brand_string, "i5-8200Y") || strstr(brand_string, "i7-8500Y")) {
    return std::unique_ptr<PlatformConfiguration>(
        new PlatformConfiguration(kAtlasPL1MinMw, kAtlasPL1MaxMw));
  }
  return nullptr;
}

zx_status_t PlatformConfiguration::SetPL1Mw(uint32_t target_mw) {
  zx_system_powerctl_arg_t arg = {
      .x86_power_limit =
          {
              .power_limit = target_mw,
              .time_window = 0,
              .clamp = 1,
              .enable = 1,
          },
  };
  zx_status_t st = zx_system_powerctl(root_resource, ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1, &arg);
  if (st != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to set PL1 to %d: %d\n", target_mw, st);
    return st;
  }
  current_pl1_mw_ = target_mw;
  TRACE_COUNTER("thermal", "throttle", 0, "pl1", target_mw);
  return ZX_OK;
}

static zx_status_t thermal_device_added(int dirfd, int event, const char* name, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  if (!strcmp("000", name)) {
    // Device found, terminate watcher
    return ZX_ERR_STOP;
  } else {
    return ZX_OK;
  }
}

static void start_trace(void) {
  // Create a message loop
  static async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  static trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  static bool started = false;
  if (!started) {
    printf("thermd: start trace\n");
    loop.StartThread();
    started = true;
  }
}

int main(int argc, char** argv) {
  auto config = PlatformConfiguration::Create();
  if (!config) {
    return 0;
  }

  printf("thermd: started\n");

  start_trace();

  zx_status_t st = get_root_resource(&root_resource);
  if (st != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get root resource: %d\n", st);
    return -1;
  }

  zx_nanosleep(zx_deadline_after(ZX_SEC(3)));

  int dirfd = open("/dev/class/thermal", O_DIRECTORY | O_RDONLY);
  if (dirfd < 0) {
    fprintf(stderr, "ERROR: Failed to open /dev/class/thermal: %d (errno %d)\n", dirfd, errno);
    return -1;
  }

  st = fdio_watch_directory(dirfd, thermal_device_added, ZX_TIME_INFINITE, NULL);

  if (st != ZX_ERR_STOP) {
    fprintf(stderr,
            "ERROR: watcher terminating without finding sensors, "
            "terminating thermd...\n");
    return -1;
  }

  // first sensor is ambient sensor
  // TODO: come up with a way to detect this is the ambient sensor
  fbl::unique_fd fd(open("/dev/class/thermal/000", O_RDWR));
  if (fd.get() < 0) {
    fprintf(stderr, "ERROR: Failed to open sensor: %d\n", fd.get());
    return -1;
  }

  fdio_cpp::FdioCaller caller(std::move(fd));

  zx_status_t status2;
  float temp;
  st = fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(caller.borrow_channel(), &status2,
                                                            &temp);
  if (st != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get temperature: %d %d\n", st, status2);
    return -1;
  }
  TRACE_COUNTER("thermal", "temp", 0, "ambient-c", temp);

  fuchsia_hardware_thermal_ThermalInfo info;
  st = fuchsia_hardware_thermal_DeviceGetInfo(caller.borrow_channel(), &status2, &info);
  if (st != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %d %d\n", st, status2);
    return -1;
  }

  TRACE_COUNTER("thermal", "trip-point", 0, "passive-c", info.passive_temp_celsius, "critical-c",
                info.critical_temp_celsius);

  zx_handle_t h = ZX_HANDLE_INVALID;
  st = fuchsia_hardware_thermal_DeviceGetStateChangeEvent(caller.borrow_channel(), &status2, &h);
  if (st != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get event: %d %d\n", st, status2);
    return -1;
  }

  if (info.max_trip_count == 0) {
    fprintf(stderr, "Trip points not supported, exiting\n");
    return 0;
  }

  // Set a trip point
  st = fuchsia_hardware_thermal_DeviceSetTripCelsius(caller.borrow_channel(), 0,
                                                     info.passive_temp_celsius, &status2);
  if (st != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to set trip point: %d %d\n", st, status2);
    return -1;
  }

  // Update info
  st = fuchsia_hardware_thermal_DeviceGetInfo(caller.borrow_channel(), &status2, &info);
  if (st != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %d %d\n", st, status2);
    return -1;
  }
  TRACE_COUNTER("thermal", "trip-point", 0, "passive-c", info.passive_temp_celsius, "critical-c",
                info.critical_temp_celsius, "active0-c", info.active_trip[0]);

  // Set PL1 to the platform maximum.
  config->SetMaxPL1();

  for (;;) {
    zx_signals_t observed = 0;
    st = zx_object_wait_one(h, ZX_USER_SIGNAL_0, zx_deadline_after(ZX_SEC(1)), &observed);
    if ((st != ZX_OK) && (st != ZX_ERR_TIMED_OUT)) {
      fprintf(stderr, "ERROR: Failed to wait on event: %d\n", st);
      return st;
    }
    if (observed & ZX_USER_SIGNAL_0) {
      st = fuchsia_hardware_thermal_DeviceGetInfo(caller.borrow_channel(), &status2, &info);
      if (st != ZX_OK || status2 != ZX_OK) {
        fprintf(stderr, "ERROR: Failed to get thermal info: %d %d\n", st, status2);
        return -1;
      }
      if (info.state) {
        // Decrease power limit
        config->SetMinPL1();

        st = fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(caller.borrow_channel(), &status2,
                                                                  &temp);
        if (st != ZX_OK || status2 != ZX_OK) {
          fprintf(stderr, "ERROR: Failed to get temperature: %d %d\n", st, status2);
          return -1;
        }
      } else {
        TRACE_COUNTER("thermal", "event", 0, "spurious", temp);
      }
    }
    if (st == ZX_ERR_TIMED_OUT) {
      st = fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(caller.borrow_channel(), &status2,
                                                                &temp);
      if (st != ZX_OK || status2 != ZX_OK) {
        fprintf(stderr, "ERROR: Failed to get temperature: %d %d\n", st, status2);
        return -1;
      }
      TRACE_COUNTER("thermal", "temp", 0, "ambient-c", temp);

      // Increase power limit if the temperature dropped enough
      if (temp < info.active_trip[0] - kCoolThresholdCelsius && !config->IsAtMax()) {
        // Make sure the state is clear
        st = fuchsia_hardware_thermal_DeviceGetInfo(caller.borrow_channel(), &status2, &info);
        if (st != ZX_OK || status2 != ZX_OK) {
          fprintf(stderr, "ERROR: Failed to get thermal info: %d %d\n", st, status2);
          return -1;
        }
        if (!info.state) {
          config->SetMaxPL1();
        }
      }

      if (temp > info.active_trip[0] && !config->IsAtMin()) {
        // Decrease power limit
        config->SetMinPL1();
      }
    }
  }

  printf("thermd terminating: %d\n", st);

  return 0;
}
