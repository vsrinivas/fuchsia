// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/gpu/clock/c/fidl.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <inttypes.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/system.h>

#include <lib/zx/handle.h>

// TODO(braval): Combine thermd & thermd_arm and have a unified
// code for the thermal deamon
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

int main(int argc, char** argv) {
  printf("thermd: started\n");

  // TODO(braval): This sleep is not needed here but leaving it here
  // since the Intel thermd has it. Clean up when both deamons are
  // unified
  zx_nanosleep(zx_deadline_after(ZX_SEC(3)));

  int dirfd = open("/dev/class/thermal", O_DIRECTORY | O_RDONLY);
  if (dirfd < 0) {
    fprintf(stderr, "ERROR: Failed to open /dev/class/thermal: %d (errno %d)\n", dirfd, errno);
    return -1;
  }

  zx_status_t st = fdio_watch_directory(dirfd, thermal_device_added, ZX_TIME_INFINITE, NULL);
  if (st != ZX_ERR_STOP) {
    fprintf(stderr,
            "ERROR: watcher terminating without finding sensors, "
            "terminating thermd...\n");
    return -1;
  }

  // first device is the one we are interested
  int fd = open("/dev/class/thermal/000", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "ERROR: Failed to open sensor: %d (errno %d) \n", fd, errno);
    return -1;
  }

  // first device is the one we are interested
  int fd_gpu = open("/dev/class/gpu-thermal/000", O_RDONLY);
  if (fd_gpu < 0) {
    fprintf(stderr, "ERROR: Failed to open gpu: %d (errno %d) \n", fd_gpu, errno);
    return -1;
  }

  zx::handle handle;
  st = fdio_get_service_handle(fd, handle.reset_and_get_address());
  if (st != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get thermal service: %d\n", st);
    return -1;
  }

  zx::handle gpu_handle;
  st = fdio_get_service_handle(fd_gpu, gpu_handle.reset_and_get_address());
  if (st != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get gpu service: %d\n", st);
    return -1;
  }

  zx_status_t status2;
  fuchsia_hardware_thermal_ThermalDeviceInfo info;
  st = fuchsia_hardware_thermal_DeviceGetDeviceInfo(handle.get(), &status2, &info);
  if (st != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %d %d\n", st, status2);
    return -1;
  }

  if (info.num_trip_points == 0) {
    fprintf(stderr, "Trip points not supported, exiting\n");
    return 0;
  }

  if (!info.active_cooling && !info.passive_cooling) {
    fprintf(stderr,
            "ERROR: No active or passive cooling present on device, "
            "terminating thermd...\n");
    return 0;
  }

  zx_handle_t port = ZX_HANDLE_INVALID;
  st = fuchsia_hardware_thermal_DeviceGetStateChangePort(handle.get(), &status2, &port);
  if (st != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get event: %d %d\n", st, status2);
    return -1;
  }

  for (;;) {
    zx_port_packet_t packet;
    st = zx_port_wait(port, ZX_TIME_INFINITE, &packet);
    if (st != ZX_OK) {
      fprintf(stderr, "ERROR: Failed to wait on port: %d\n", st);
      return st;
    }

    uint32_t trip_idx = (uint32_t)packet.key;
    if (trip_idx > info.num_trip_points) {
      fprintf(stderr, "ERROR: Invalid trip index: terminating thermd\n");
      return -1;
    }

    if (info.passive_cooling) {
      uint32_t big_cluster_opp = info.trip_point_info[trip_idx].big_cluster_dvfs_opp;

      // Set DVFS Opp for Big Cluster.
      st = fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
          handle.get(), big_cluster_opp,
          fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status2);
      if (st != ZX_OK || status2 != ZX_OK) {
        fprintf(stderr, "ERROR: Failed to set DVFS OPP for big cluster: %d %d\n", st, status2);
        return -1;
      }

      // Check if it's big little.
      if (info.big_little) {
        // Set the DVFS Opp for Little Cluster.
        uint32_t little_cluster_opp = info.trip_point_info[trip_idx].little_cluster_dvfs_opp;
        st = fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
            handle.get(), little_cluster_opp,
            fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, &status2);
        if (st != ZX_OK || status2 != ZX_OK) {
          fprintf(stderr, "ERROR: Failed to set DVFS OPP for little cluster: %d %d\n", st, status2);
          return -1;
        }
      }
    }

    if (info.active_cooling) {
      uint32_t fan_level = info.trip_point_info[trip_idx].fan_level;
      st = fuchsia_hardware_thermal_DeviceSetFanLevel(handle.get(), fan_level, &status2);
      if (st != ZX_OK || status2 != ZX_OK) {
        fprintf(stderr, "ERROR: Failed to set fan level: %d %d\n", st, status2);
      }
    }

    if (info.gpu_throttling) {
      int gpu_clk_freq_source = info.trip_point_info[trip_idx].gpu_clk_freq_source;
      if (gpu_clk_freq_source != -1) {
        st = fuchsia_hardware_gpu_clock_ClockSetFrequencySource(gpu_handle.get(),
                                                                gpu_clk_freq_source, &status2);
        if (st != ZX_OK || status2 != ZX_OK) {
          fprintf(stderr, "ERROR: Failed to change gpu clock freq source: %d %d\n", st, status2);
          return -1;
        }
      }
    }
  }

  close(fd);

  printf("thermd terminating: %d\n", st);

  return 0;
}
