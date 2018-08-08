// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/watcher.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/gpu.h>
#include <zircon/device/sysinfo.h>
#include <zircon/device/thermal.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/system.h>

// TODO(braval): Combine thermd & thermd_arm and have a unified
// code for the thermal deamon
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

int main(int argc, char** argv) {
  printf("thermd: started\n");

  // TODO(braval): This sleep is not needed here but leaving it here
  // since the Intel thermd has it. Clean up when both deamons are
  // unified
  zx_nanosleep(zx_deadline_after(ZX_SEC(3)));

  int dirfd = open("/dev/class/thermal", O_DIRECTORY | O_RDONLY);
  if (dirfd < 0) {
    fprintf(stderr, "ERROR: Failed to open /dev/class/thermal: %d (errno %d)\n",
            dirfd, errno);
    return -1;
  }

  zx_status_t st =
      fdio_watch_directory(dirfd, thermal_device_added, ZX_TIME_INFINITE, NULL);
  if (st != ZX_ERR_STOP) {
    fprintf(stderr,
            "ERROR: watcher terminating without finding sensors, "
            "terminating thermd...\n");
    return -1;
  }

  // first device is the one we are interested
  int fd = open("/dev/class/thermal/000", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "ERROR: Failed to open sensor: %d (errno %d) \n", fd,
            errno);
    return -1;
  }

  // first device is the one we are interested
  int fd_gpu = open("/dev/class/gpu-thermal/000", O_RDONLY);
  if (fd_gpu < 0) {
    fprintf(stderr, "ERROR: Failed to open gpu: %d (errno %d) \n", fd_gpu,
            errno);
    return -1;
  }

  thermal_device_info_t info;
  ssize_t rc = ioctl_thermal_get_device_info(fd, &info);
  if (rc != sizeof(info)) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %zd\n", rc);
    return rc;
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
  rc = ioctl_thermal_get_state_change_port(fd, &port);
  if (rc != sizeof(port)) {
    fprintf(stderr, "ERROR: Failed to get event: %zd\n", rc);
    return rc;
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
      fprintf(stderr, "Invalid trip index: terminating thermd\n");
      return -1;
    }

    if (info.passive_cooling) {
      int32_t big_cluster_opp =
          info.trip_point_info[trip_idx].big_cluster_dvfs_opp;
      int32_t little_cluster_opp =
          info.trip_point_info[trip_idx].little_cluster_dvfs_opp;
      dvfs_info_t dvfs_info;

      if (big_cluster_opp != -1) {
        dvfs_info.power_domain = BIG_CLUSTER_POWER_DOMAIN;
        dvfs_info.op_idx = big_cluster_opp;
        rc = ioctl_thermal_set_dvfs_opp(fd, &dvfs_info);
        if (rc) {
          fprintf(stderr,
                  "ERROR: Failed to set DVFS OPP for big cluster: %zd\n", rc);
          return rc;
        }
      }

      if (little_cluster_opp != -1) {
        dvfs_info.power_domain = LITTLE_CLUSTER_POWER_DOMAIN;
        dvfs_info.op_idx = little_cluster_opp;
        rc = ioctl_thermal_set_dvfs_opp(fd, &dvfs_info);
        if (rc) {
          fprintf(stderr,
                  "ERROR: Failed to set DVFS OPP for little cluster: %zd\n",
                  rc);
          return rc;
        }
      }
    }

    if (info.active_cooling) {
      int32_t fan_level = info.trip_point_info[trip_idx].fan_level;
      if (fan_level != -1) {
        rc = ioctl_thermal_set_fan_level(fd, &fan_level);
        if (rc) {
          fprintf(stderr, "ERROR: Failed to set fan level: %zd\n", rc);
          return rc;
        }
      }
    }

    if (info.gpu_throttling) {
      int gpu_clk_freq_source =
          info.trip_point_info[trip_idx].gpu_clk_freq_source;
      if (gpu_clk_freq_source != -1) {
        rc = ioctl_gpu_set_clk_freq_source(fd_gpu, &gpu_clk_freq_source);
        if (rc) {
          fprintf(stderr,
                  "ERROR: Failed to change gpu clock freq source: %zd\n", rc);
          return rc;
        }
      }
    }
  }

  close(fd);

  printf("thermd terminating: %d\n", st);

  return 0;
}
