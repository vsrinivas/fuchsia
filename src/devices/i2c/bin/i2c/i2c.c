// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/i2c/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

const char* prog_name;

void print_usage(void) {
  printf("Usage:\n");
  printf("\n");
  printf("%s DEVICE COMMAND [command arguments]\n", prog_name);
  printf("DEVICE is either the i2c bus or i2c slave COMMAND applies to.\n");
  printf("COMMAND is one of the following commands, optionally followed \n");
  printf("arguments which are specific to each command.\n");
  printf("\n");
  printf("read LENGTH: Read data from the target slave device.\n");
  printf("LENGTH is the number of bytes to read in decimal.\n");
  printf("\n");
  printf("write [data]: Write data to the target slave device.\n");
  printf("data is a sequence of hex values which each represent one byte\n");
  printf("of data to write to the target device.\n");
  printf("\n");
  printf("transfer [segments]: Perform a transfer to/from the i2c slave.\n");
  printf("segments is a series of segment descriptions which are a\n");
  printf("direction, a length, and then (for writes) a series of bytes\n");
  printf("in hexadecimal.\n");
  printf("\n");
  printf("The direction is specified as either \"w\" for writes, or\n");
  printf("\"r\" for reads.\n");
  printf("\n");
  printf("For example, to perform a write of one byte and then a read\n");
  printf("of one byte without giving up the bus:\n");
  printf("%s [dev] transfer w 1 00 r 1\n", prog_name);
}

int cmd_read(int fd, int argc, const char** argv) {
  if (argc < 1) {
    print_usage();
    return 1;
  }

  errno = 0;
  long int length = strtol(argv[0], NULL, 10);
  if (errno) {
    print_usage();
    return errno;
  }

  uint8_t* buf = malloc(length);
  if (!buf) {
    printf("Failed to allocate buffer.\n");
    return 1;
  }

  int ret = read(fd, buf, length);
  if (ret < 0) {
    printf("Error reading from slave. (%d)\n", ret);
    goto cmd_read_finish;
  }

  for (int i = 0; i < length; i++) {
    printf(" %02x", buf[i]);
    if (i % 32 == 31)
      printf("\n");
  }
  printf("\n");

cmd_read_finish:
  free(buf);
  return ret;
}

int cmd_write(int fd, int argc, const char** argv) {
  if (argc < 1) {
    print_usage();
    return 1;
  }

  uint8_t* buf = malloc(argc);
  if (!buf) {
    printf("Failed to allocate buffer.\n");
    return 1;
  }

  int ret = 0;

  errno = 0;
  for (int i = 0; i < argc; i++) {
    buf[i] = strtol(argv[i], NULL, 16);
    if (errno) {
      ret = errno;
      print_usage();
      goto cmd_write_finish;
    }
  }

  ret = write(fd, buf, argc);
  if (ret < 0)
    printf("Error writing to slave. (%d)\n", ret);

cmd_write_finish:
  free(buf);
  return ret;
}

int cmd_transfer(int fd, int argc, const char** argv) {
  const size_t base_size = sizeof(fuchsia_hardware_i2c_Segment);
  int ret = ZX_OK;

  // Figure out how big our buffers need to be.
  // Start the counters with enough space for the fuchsia_hardware_i2c_SegmentType_END
  // segment.
  size_t in_len = base_size;
  size_t out_len = 0;
  int segments = 1;
  int count = argc;
  const char** arg = argv;
  errno = 0;
  while (count) {
    if (count < 2) {
      print_usage();
      goto cmd_transfer_finish_2;
    }

    in_len += base_size;

    int read;
    if (!strcmp(arg[0], "r")) {
      read = 1;
    } else if (!strcmp(arg[0], "w")) {
      read = 0;
    } else {
      print_usage();
      goto cmd_transfer_finish_2;
    }
    segments++;

    long int length = strtol(arg[1], NULL, 10);
    if (errno) {
      print_usage();
      return errno;
    }
    arg += 2;
    count -= 2;
    if (read) {
      out_len += length;
    } else {
      in_len += length;
      if (length > count) {
        print_usage();
        goto cmd_transfer_finish_2;
      }
      arg += length;
      count -= length;
    }
  }

  // Allocate the input and output buffers.
  void* in_buf = malloc(in_len);
  void* out_buf = malloc(out_len);
  if (!in_buf || !out_buf) {
    ret = 1;
    goto cmd_transfer_finish_1;
  }
  uint8_t* data_addr = (uint8_t*)in_buf + segments * base_size;
  uint8_t* data_buf = data_addr;

  // Fill the "input" buffer which is sent via FIDL.
  uintptr_t in_addr = (uintptr_t)in_buf;
  int i = 0;
  fuchsia_hardware_i2c_Segment* segment = (fuchsia_hardware_i2c_Segment*)in_addr;
  while (i < argc) {
    if (!strcmp(argv[i++], "r")) {
      segment->type = fuchsia_hardware_i2c_SegmentType_READ;
      segment->len = strtol(argv[i++], NULL, 10);
      if (errno) {
        print_usage();
        return errno;
      }
    } else {
      segment->type = fuchsia_hardware_i2c_SegmentType_WRITE;
      segment->len = strtol(argv[i++], NULL, 10);
      if (errno) {
        print_usage();
        return errno;
      }

      for (uint32_t seg = 0; seg < segment->len; seg++) {
        *data_buf++ = strtol(argv[i++], NULL, 16);
        if (errno) {
          print_usage();
          return errno;
        }
      }
    }
    segment++;
  }
  segment->type = fuchsia_hardware_i2c_SegmentType_END;
  segment->len = 0;
  segment++;
  // We should be at the start of the data section now.
  if ((uint8_t*)segment != data_addr) {
    ret = 1;
    goto cmd_transfer_finish_1;
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd);  // Get a fdio channel from the fd.
  if (io == NULL)
    return ZX_ERR_INVALID_ARGS;
  zx_status_t status;
  size_t out_actual;
  zx_status_t res = fuchsia_hardware_i2c_DeviceSubordinateTransfer(
      fdio_unsafe_borrow_channel(io), in_buf, in_len, &status, out_buf, out_len, &out_actual);
  fdio_unsafe_release(io);  // Release the channel manually.
  if (out_actual < out_len) {
    status = ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (res == ZX_OK) {
    res = status;
  }
  if (res != ZX_OK) {
    printf("Error in transfer to/from slave. (%d)\n", res);
    goto cmd_transfer_finish_1;
  }

  for (size_t i = 0; i < out_len; i++) {
    printf(" %02x", ((uint8_t*)out_buf)[i]);
    if (i % 32 == 31)
      printf("\n");
  }
  printf("\n");

  ret = 0;

cmd_transfer_finish_1:
  free(in_buf);
  free(out_buf);
cmd_transfer_finish_2:
  return ret;
}

int main(int argc, const char** argv) {
  if (argc < 1)
    return 1;

  prog_name = argv[0];

  if (argc < 3) {
    print_usage();
    return 1;
  }

  const char* dev = argv[1];
  const char* cmd = argv[2];

  argc -= 3;
  argv += 3;

  int fd = open(dev, O_RDWR);
  if (fd < 0) {
    printf("Error opening I2C device.\n");
    return 1;
  }

  if (!strcmp("read", cmd)) {
    return cmd_read(fd, argc, argv);
  } else if (!strcmp("write", cmd)) {
    return cmd_write(fd, argc, argv);
  } else if (!strcmp("transfer", cmd)) {
    return cmd_transfer(fd, argc, argv);
  } else {
    printf("Unrecognized command %s.\n", cmd);
    print_usage();
    return 1;
  }

  printf("We should never get here!.\n");
  return 1;
}
