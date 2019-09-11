// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/backlight/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(char* argv[]) {
  printf("Usage: %s [--read|--off|<brightness-val>]\n", argv[0]);
  printf("options:\n    <brightness-val>: 0.0-1.0\n");
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    usage(argv);
    return -1;
  }

  int fd = open("/dev/class/backlight/000", O_RDONLY);
  if (fd < 0) {
    printf("Failed to open backlight\n");
    return -1;
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (io == NULL) {
    printf("Failed to get fdio_t\n");
    return -1;
  }
  zx_handle_t channel = fdio_unsafe_borrow_channel(io);

  if (strcmp(argv[1], "--read") == 0) {
    fuchsia_hardware_backlight_State state;
    zx_status_t status = fuchsia_hardware_backlight_DeviceGetState(channel, &state);
    if (status != ZX_OK) {
      printf("Get backlight state failed %d\n", status);
      return -1;
    }
    printf("Backlight:%s Brightness:%f\n", state.backlight_on ? "on" : "off", state.brightness);
    return 0;
  }

  bool on;
  double brightness;
  if (strcmp(argv[1], "--off") == 0) {
    on = false;
    brightness = 0.0f;
  } else {
    char* endptr;
    brightness = strtod(argv[1], &endptr);
    if (endptr == argv[1] || *endptr != '\0') {
      usage(argv);
      return -1;
    }
    if (brightness < 0.0f || brightness > 1.0f) {
      printf("Invalid brightness %f\n", brightness);
      return -1;
    }
    on = true;
  }

  fuchsia_hardware_backlight_State state = {.backlight_on = on, .brightness = brightness};
  zx_status_t status = fuchsia_hardware_backlight_DeviceSetState(channel, &state);
  if (status != ZX_OK) {
    printf("Set brightness failed %d\n", status);
    return -1;
  }

  return 0;
}
