// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/types.h>

#define CLEAR_SCREEN printf("\033[2J")
#define CURSOR_MOVE(r, c) printf("\033[%d;%dH", r, c)
#define CLEAR_LINE printf("\033[2K")

static void process_sensor_input(void* buf, size_t len) {
  uint8_t* report = buf;
  if (len < 1) {
    printf("bad report size: %zd < %d\n", len, 1);
    return;
  }

  uint8_t report_id = report[0];
  CURSOR_MOVE(report_id + 1, 0);
  CLEAR_LINE;

  // TODO(teisenbe): Once we can decode these reports, output them decoded.
  printf("%3d:", report_id);
  for (size_t i = 1; i < len; ++i) {
    printf(" %02x", report[i]);
  }
  printf("\n");
  fflush(stdout);
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Usage: %s /dev/class/input/<id>\n", argv[0]);
    return -1;
  }

  void* buf = NULL;
  uint8_t* rpt_desc = NULL;
  const char* devname = argv[1];
  int fd = open(devname, O_RDONLY);
  if (fd < 0) {
    printf("failed to open %s: %d\n", devname, errno);
    return -1;
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (io == NULL) {
    printf("failed to convert fd\n");
    return -1;
  }
  zx_handle_t svc = fdio_unsafe_borrow_channel(io);

  int ret;
  uint16_t rpt_desc_len = 0;

  zx_status_t status = fuchsia_hardware_input_DeviceGetReportDescSize(svc, &rpt_desc_len);
  if (status != ZX_OK) {
    printf("failed to get report descriptor length for %s: %d\n", devname, status);
    ret = -1;
    goto cleanup;
  }

  rpt_desc = malloc(rpt_desc_len);
  if (rpt_desc == NULL) {
    printf("no memory!\n");
    ret = -1;
    goto cleanup;
  }

  size_t actual = 0;
  status = fuchsia_hardware_input_DeviceGetReportDesc(svc, rpt_desc, rpt_desc_len, &actual);
  if (status != ZX_OK) {
    printf("failed to get report descriptor for %s: %d\n", devname, status);
    ret = -1;
    goto cleanup;
  }

  assert(rpt_desc_len > 0);
  assert(rpt_desc_len == actual);
  assert(rpt_desc);

  uint16_t max_rpt_sz = 0;
  status = fuchsia_hardware_input_DeviceGetMaxInputReportSize(svc, &max_rpt_sz);
  if (status != ZX_OK) {
    printf("failed to get max report size: %d\n", status);
    ret = -1;
    goto cleanup;
  }
  buf = malloc(max_rpt_sz);
  if (buf == NULL) {
    printf("no memory!\n");
    ret = -1;
    goto cleanup;
  }

  CLEAR_SCREEN;
  fflush(stdout);
  while (1) {
    ssize_t r = read(fd, buf, max_rpt_sz);
    if (r < 0) {
      printf("sensor read error: %zd (errno=%d)\n", r, errno);
      break;
    }

    process_sensor_input(buf, r);
  }

  ret = 0;
cleanup:
  free(buf);
  free(rpt_desc);
  fdio_unsafe_release(io);
  close(fd);
  return ret;
}
