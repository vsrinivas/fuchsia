// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/rtc/c/fidl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/syscalls.h>

int usage(const char *cmd) {
  fprintf(stderr,
          "Interact with the real-time or monotonic clocks:\n"
          "   %s                              Print the time\n"
          "   %s --help                       Print this message\n"
          "   %s --set YYYY-mm-ddThh:mm:ss    Set the time\n"
          "   %s --monotonic                  Print nanoseconds since boot\n"
          "   optionally specify an RTC device with --dev PATH_TO_DEVICE_NODE\n",
          cmd, cmd, cmd, cmd);
  return -1;
}

char *guess_dev(void) {
  char path[19];  // strlen("/dev/class/rtc/###") + 1
  DIR *d = opendir("/dev/class/rtc");
  if (!d) {
    return NULL;
  }

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (strlen(de->d_name) != 3) {
      continue;
    }

    if (isdigit(de->d_name[0]) && isdigit(de->d_name[1]) && isdigit(de->d_name[2])) {
      sprintf(path, "/dev/class/rtc/%.3s", de->d_name);
      closedir(d);
      return strdup(path);
    }
  }

  closedir(d);
  return NULL;
}

zx_status_t open_rtc(const char *path, zx_handle_t *handle) {
  int rtc_fd = open(path, O_RDONLY);
  if (rtc_fd < 0) {
    printf("Can not open RTC device\n");
  }
  return fdio_get_service_handle(rtc_fd, handle);
}

int print_rtc(const char *path) {
  zx_handle_t handle;
  zx_status_t status = open_rtc(path, &handle);
  if (status != ZX_OK) {
    return -1;
  }
  fuchsia_hardware_rtc_Time rtc;

  status = fuchsia_hardware_rtc_DeviceGet(handle, &rtc);
  if (status != ZX_OK) {
    return -1;
  }
  printf("%04d-%02d-%02dT%02d:%02d:%02d\n", rtc.year, rtc.month, rtc.day, rtc.hours, rtc.minutes,
         rtc.seconds);
  return 0;
}

int set_rtc(const char *path, const char *time) {
  fuchsia_hardware_rtc_Time rtc;
  int n = sscanf(time, "%04hd-%02hhd-%02hhdT%02hhd:%02hhd:%02hhd", &rtc.year, &rtc.month, &rtc.day,
                 &rtc.hours, &rtc.minutes, &rtc.seconds);
  if (n != 6) {
    printf("Bad time format.\n");
    return -1;
  }
  zx_handle_t handle;
  zx_status_t status = open_rtc(path, &handle);
  if (status != ZX_OK) {
    printf("Can not open RTC device\n");
    return status;
  }

  zx_status_t set_status;
  status = fuchsia_hardware_rtc_DeviceSet(handle, &rtc, &set_status);
  if (status != ZX_OK) {
    return status;
  }

  return set_status;
}

void print_monotonic(void) { printf("%lu\n", zx_clock_get_monotonic()); }

int main(int argc, char **argv) {
  int err = 0;
  const char *cmd = basename(argv[0]);
  char *path = NULL;
  char *set = NULL;
  static const struct option opts[] = {
      {"set", required_argument, NULL, 's'},
      {"dev", required_argument, NULL, 'd'},
      {"monotonic", no_argument, NULL, 'm'},
      {"help", no_argument, NULL, 'h'},
      {0},
  };
  for (int opt; (opt = getopt_long(argc, argv, "", opts, NULL)) != -1;) {
    switch (opt) {
      case 's':
        set = strdup(optarg);
        break;
      case 'd':
        path = strdup(optarg);
        break;
      case 'm':
        print_monotonic();
        goto done;
      case 'h':
        usage(cmd);
        err = 0;
        goto done;
      default:
        err = usage(cmd);
        goto done;
    }
  }

  argv += optind;
  argc -= optind;

  if (argc != 0) {
    err = usage(cmd);
    goto done;
  }

  if (!path) {
    path = guess_dev();
    if (!path) {
      fprintf(stderr, "No RTC found.\n");
      err = usage(cmd);
      goto done;
    }
  }

  if (set) {
    err = set_rtc(path, set);
    if (err) {
      printf("Set RTC failed.\n");
      usage(cmd);
    }
    goto done;
  }

  err = print_rtc(path);
  if (err) {
    usage(cmd);
  }

done:
  free(path);
  free(set);
  return err;
}
