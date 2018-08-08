// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/sysinfo.h>
#include <zircon/device/thermal.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/system.h>

#include <lib/async-loop/cpp/loop.h>

#include <lib/fdio/watcher.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

#include <cpuid.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static zx_handle_t root_resource;

static uint32_t pl1_mw;  // current PL1 value

#define PL1_MIN 2500
#define PL1_MAX 7000

static constexpr uint32_t COOL_TEMP_THRESHOLD =
    50;  // degrees in kelvins below threshold before
         // we adjust PL value

static zx_status_t get_root_resource(zx_handle_t* root_resource) {
  int fd = open("/dev/misc/sysinfo", O_RDWR);
  if (fd < 0) {
    return ZX_ERR_NOT_FOUND;
  }

  ssize_t n = ioctl_sysinfo_get_root_resource(fd, root_resource);
  close(fd);
  if (n != sizeof(*root_resource)) {
    if (n < 0) {
      return (zx_status_t)n;
    } else {
      return ZX_ERR_NOT_FOUND;
    }
  }
  return ZX_OK;
}

static zx_status_t set_pl1(uint32_t target) {
  zx_system_powerctl_arg_t arg = {
      .x86_power_limit =
          {
              .power_limit = target,
              .time_window = 0,
              .clamp = 1,
              .enable = 1,
          },
  };
  zx_status_t st = zx_system_powerctl(root_resource,
                                      ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1, &arg);
  if (st != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to set PL1 to %d: %d\n", target, st);
    return st;
  }
  pl1_mw = target;
  TRACE_COUNTER("thermal", "throttle", 0, "pl1", target);
  return ZX_OK;
}

static uint32_t to_celsius(uint32_t val) {
  // input is 10th of a kelvin
  return (val * 10 - 27315) / 100;
}

static uint32_t to_kelvin(uint32_t celsius) __attribute__((unused));

static uint32_t to_kelvin(uint32_t celsius) {
  // return in 10th of a kelvin
  return (celsius * 100 + 27315) / 10;
}

static zx_status_t thermal_device_added(int dirfd, int event, const char* name,
                                        void* cookie) {
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
  static async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  static trace::TraceProvider trace_provider(loop.dispatcher());
  static bool started = false;
  if (!started) {
    printf("thermd: start trace\n");
    loop.StartThread();
    started = true;
  }
}

static bool check_platform() {
  unsigned int a, b, c, d;
  unsigned int leaf_num = 0x80000002;
  char brand_string[50];
  memset(brand_string, 0, sizeof(brand_string));
  for (int i = 0; i < 3; i++) {
    if (!__get_cpuid(leaf_num + i, &a, &b, &c, &d)) {
      return false;
    }
    memcpy(brand_string + (i * 16), &a, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 4, &b, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 8, &c, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 12, &d, sizeof(uint32_t));
  }
  // Only run thermd for processors used in Pixelbooks. The PL1 min/max settings
  // are specified by the chipset.
  if (strstr(brand_string, "i5-7Y57") || strstr(brand_string, "i7-7Y75")) {
    return true;
  } else {
    return false;
  }
}

int main(int argc, char** argv) {
  if (!check_platform()) {
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
    fprintf(stderr, "ERROR: Failed to open /dev/class/thermal: %d (errno %d)\n",
            dirfd, errno);
    return -1;
  }

  st =
      fdio_watch_directory(dirfd, thermal_device_added, ZX_TIME_INFINITE, NULL);

  if (st != ZX_ERR_STOP) {
    fprintf(stderr,
            "ERROR: watcher terminating without finding sensors, "
            "terminating thermd...\n");
    return -1;
  }

  // first sensor is ambient sensor
  // TODO: come up with a way to detect this is the ambient sensor
  int fd = open("/dev/class/thermal/000", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "ERROR: Failed to open sensor: %d\n", fd);
    return -1;
  }

  uint32_t temp;
  ssize_t rc = read(fd, &temp, sizeof(temp));
  if (rc != sizeof(temp)) {
    return rc;
  }
  TRACE_COUNTER("thermal", "temp", 0, "ambient-c", to_celsius(temp));

  thermal_info_t info;
  rc = ioctl_thermal_get_info(fd, &info);
  if (rc != sizeof(info)) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %zd\n", rc);
    return rc;
  }

  TRACE_COUNTER("thermal", "trip-point", 0, "passive-c",
                to_celsius(info.passive_temp), "critical-c",
                to_celsius(info.critical_temp));

  zx_handle_t h = ZX_HANDLE_INVALID;
  rc = ioctl_thermal_get_state_change_event(fd, &h);
  if (rc != sizeof(h)) {
    fprintf(stderr, "ERROR: Failed to get event: %zd\n", rc);
    return rc;
  }

  if (info.max_trip_count == 0) {
    fprintf(stderr, "Trip points not supported, exiting\n");
    return 0;
  }

  // Set a trip point
  trip_point_t tp = {
      .id = 0,
      .temp = info.passive_temp,
  };
  rc = ioctl_thermal_set_trip(fd, &tp);
  if (rc) {
    fprintf(stderr, "ERROR: Failed to set trip point: %zd\n", rc);
    return rc;
  }

  // Update info
  rc = ioctl_thermal_get_info(fd, &info);
  if (rc != sizeof(info)) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %zd\n", rc);
    return rc;
  }
  TRACE_COUNTER("thermal", "trip-point", 0, "passive-c",
                to_celsius(info.passive_temp), "critical-c",
                to_celsius(info.critical_temp), "active0-c",
                to_celsius(info.active_trip[0]));

  // set PL1 to 7 watts (EDP)
  set_pl1(PL1_MAX);

  for (;;) {
    zx_signals_t observed = 0;
    st = zx_object_wait_one(h, ZX_USER_SIGNAL_0, zx_deadline_after(ZX_SEC(1)),
                            &observed);
    if ((st != ZX_OK) && (st != ZX_ERR_TIMED_OUT)) {
      fprintf(stderr, "ERROR: Failed to wait on event: %d\n", st);
      return st;
    }
    if (observed & ZX_USER_SIGNAL_0) {
      rc = ioctl_thermal_get_info(fd, &info);
      if (rc != sizeof(info)) {
        fprintf(stderr, "ERROR: Failed to get thermal info: %zd\n", rc);
        return rc;
      }
      if (info.state) {
        set_pl1(PL1_MIN);  // decrease power limit

        rc = read(fd, &temp, sizeof(temp));
        if (rc != sizeof(temp)) {
          fprintf(stderr, "ERROR: Failed to read temperature: %zd\n", rc);
          return rc;
        }
      } else {
        TRACE_COUNTER("thermal", "event", 0, "spurious", to_celsius(temp));
      }
    }
    if (st == ZX_ERR_TIMED_OUT) {
      rc = read(fd, &temp, sizeof(temp));
      if (rc != sizeof(temp)) {
        fprintf(stderr, "ERROR: Failed to read temperature: %zd\n", rc);
        return rc;
      }
      TRACE_COUNTER("thermal", "temp", 0, "ambient-c", to_celsius(temp));

      // increase power limit if the temperature dropped enough
      if ((temp < info.active_trip[0] - COOL_TEMP_THRESHOLD) &&
          (pl1_mw != PL1_MAX)) {
        // make sure the state is clear
        rc = ioctl_thermal_get_info(fd, &info);
        if (rc != sizeof(info)) {
          fprintf(stderr, "ERROR: Failed to get thermal info: %zd\n", rc);
          return rc;
        }
        if (!info.state) {
          set_pl1(PL1_MAX);
        }
      }

      if ((temp > info.active_trip[0]) && (pl1_mw != PL1_MIN)) {
        set_pl1(PL1_MIN);  // decrease power limit
      }
    }
  }

  close(fd);

  printf("thermd terminating: %d\n", st);

  return 0;
}
