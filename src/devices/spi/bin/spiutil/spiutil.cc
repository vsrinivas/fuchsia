// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/spi/spi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>

void usage(char* prog) {
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "    %s DEVICE r LENGTH\n", prog);
  fprintf(stderr, "    %s DEVICE w BYTES ...\n", prog);
  fprintf(stderr, "    %s DEVICE x BYTES ...\n", prog);
}

void convert_args(char** argv, size_t length, uint8_t* buffer) {
  for (size_t i = 0; i < length; i++) {
    buffer[i] = static_cast<uint8_t>(strtoul(argv[i], nullptr, 0));
  }
}

void print_buffer(uint8_t* buffer, size_t length) {
  char ascii[16];
  char* a = ascii;
  size_t i;

  for (i = 0; i < length; i++) {
    if (i % 16 == 0) {
      printf("%04zx: ", i);
      a = ascii;
    }

    printf("%02x ", buffer[i]);
    if (isprint(buffer[i])) {
      *a++ = buffer[i];
    } else {
      *a++ = '.';
    }

    if (i % 16 == 15) {
      printf("|%.16s|\n", ascii);
    } else if (i % 8 == 7) {
      printf(" ");
    }
  }

  int rem = i % 16;
  if (rem != 0) {
    int spaces = (16 - rem) * 3;
    if (rem < 8) {
      spaces++;
    }
    printf("%*s|%.*s|\n", spaces, "", rem, ascii);
  }
}

int main(int argc, char** argv) {
  if (argc < 4) {
    usage(argv[0]);
    return -1;
  }

  int fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
    usage(argv[0]);
    return -2;
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (io == nullptr) {
    fprintf(stderr, "%s: fdio conversion failed\n", argv[1]);
    close(fd);
    return -3;
  }

  zx_status_t status;

  switch (argv[2][0]) {
    case 'r': {
      size_t length = strtoull(argv[3], nullptr, 0);
      uint8_t buffer[length];
      status = spilib_receive(fdio_unsafe_borrow_channel(io), buffer, length);
      print_buffer(buffer, length);
      break;
    }
    case 'w': {
      size_t length = argc - 3;
      uint8_t buffer[length];
      convert_args(&argv[3], length, buffer);
      status = spilib_transmit(fdio_unsafe_borrow_channel(io), buffer, length);
      break;
    }
    case 'x': {
      size_t length = argc - 3;
      uint8_t send[length];
      uint8_t recv[length];
      convert_args(&argv[3], length, send);
      status = spilib_exchange(fdio_unsafe_borrow_channel(io), send, recv, length);
      print_buffer(recv, length);
      break;
    }
    default:
      fprintf(stderr, "%c: unrecognized command\n", argv[2][0]);
      usage(argv[0]);
      status = -4;
  }

  fdio_unsafe_release(io);
  close(fd);
  return status;
}
