// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <ramdevice-client/ramdisk.h>

#include "src/lib/storage/block_client/cpp/client.h"

static uint64_t number(const char* str) {
  char* end;
  uint64_t n = strtoull(str, &end, 10);

  uint64_t m = 1;
  switch (*end) {
    case 'G':
    case 'g':
      m = 1024 * 1024 * 1024;
      break;
    case 'M':
    case 'm':
      m = 1024 * 1024;
      break;
    case 'K':
    case 'k':
      m = 1024;
      break;
  }
  return m * n;
}

static void bytes_per_second(uint64_t bytes, uint64_t nanos) {
  double s = ((double)nanos) / ((double)1000000000);
  double rate = ((double)bytes) / s;

  const char* unit = "B";
  if (rate > 1024 * 1024) {
    unit = "MB";
    rate /= 1024 * 1024;
  } else if (rate > 1024) {
    unit = "KB";
    rate /= 1024;
  }
  fprintf(stderr, "%g %s/s\n", rate, unit);
}

static zx_duration_t iotime_posix(int is_read, int fd, size_t total, size_t bufsz) {
  void* buffer = malloc(bufsz);
  if (buffer == NULL) {
    fprintf(stderr, "error: out of memory\n");
    return ZX_TIME_INFINITE;
  }

  zx_time_t t0 = zx_clock_get_monotonic();
  size_t n = total;
  const char* fn_name = is_read ? "read" : "write";
  while (n > 0) {
    size_t xfer = (n > bufsz) ? bufsz : n;
    ssize_t r = is_read ? read(fd, buffer, xfer) : write(fd, buffer, xfer);
    if (r < 0) {
      fprintf(stderr, "error: %s() error %d\n", fn_name, errno);
      return ZX_TIME_INFINITE;
    }
    if ((size_t)r != xfer) {
      fprintf(stderr, "error: %s() %zu of %zu bytes processed\n", fn_name, r, xfer);
      return ZX_TIME_INFINITE;
    }
    n -= xfer;
  }
  zx_time_t t1 = zx_clock_get_monotonic();

  return zx_time_sub_time(t1, t0);
}

static zx_duration_t iotime_block(int is_read, int fd, size_t total, size_t bufsz) {
  if ((total % 4096) || (bufsz % 4096)) {
    fprintf(stderr, "error: total and buffer size must be multiples of 4K\n");
    return ZX_TIME_INFINITE;
  }

  return iotime_posix(is_read, fd, total, bufsz);
}

static zx_duration_t iotime_fifo(char* dev, int is_read, int fd, size_t total, size_t bufsz) {
  zx_status_t status;
  zx::vmo vmo;
  if ((status = zx::vmo::create(bufsz, 0, &vmo)) != ZX_OK) {
    fprintf(stderr, "error: out of memory %d\n", status);
    return ZX_TIME_INFINITE;
  }

  fdio_cpp::UnownedFdioCaller disk_connection(fd);
  zx::unowned_channel channel(disk_connection.borrow_channel());
  fuchsia_hardware_block_BlockInfo info;

  zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(channel->get(), &status, &info);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "error: cannot get info for '%s'\n", dev);
    return ZX_TIME_INFINITE;
  }

  zx::fifo fifo;
  io_status =
      fuchsia_hardware_block_BlockGetFifo(channel->get(), &status, fifo.reset_and_get_address());
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "error: cannot get fifo for '%s'\n", dev);
    return ZX_TIME_INFINITE;
  }

  groupid_t group = 0;

  zx::vmo dup;
  if ((status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
    fprintf(stderr, "error: cannot duplicate handle %d\n", status);
    return ZX_TIME_INFINITE;
  }

  fuchsia_hardware_block_VmoId vmoid;
  io_status = fuchsia_hardware_block_BlockAttachVmo(channel->get(), dup.release(), &status, &vmoid);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "error: cannot attach vmo for '%s'\n", dev);
    return ZX_TIME_INFINITE;
  }

  block_client::Client client(std::move(fifo));

  zx_time_t t0 = zx_clock_get_monotonic();
  size_t n = total;
  while (n > 0) {
    size_t xfer = (n > bufsz) ? bufsz : n;
    block_fifo_request_t request = {
        .opcode = static_cast<uint32_t>(is_read ? BLOCKIO_READ : BLOCKIO_WRITE),
        .reqid = 0,
        .group = group,
        .vmoid = vmoid.id,
        .length = static_cast<uint32_t>(xfer / info.block_size),
        .vmo_offset = 0,
        .dev_offset = (total - n) / info.block_size,
    };
    if ((status = client.Transaction(&request, 1)) != ZX_OK) {
      fprintf(stderr, "error: block_fifo_txn error %d\n", status);
      return ZX_TIME_INFINITE;
    }
    n -= xfer;
  }
  zx_time_t t1 = zx_clock_get_monotonic();
  return zx_time_sub_time(t1, t0);
}

static int usage(void) {
  fprintf(stderr,
          "usage: iotime <read|write> <posix|block|fifo> <device|--ramdisk> <bytes> <bufsize>\n\n"
          "        <bytes> and <bufsize> must be a multiple of 4k for block mode\n"
          "        --ramdisk only supported for block mode\n");
  return -1;
}

int main(int argc, char** argv) {
  if (argc != 6) {
    return usage();
  }

  int is_read = !strcmp(argv[1], "read");
  size_t total = number(argv[4]);
  size_t bufsz = number(argv[5]);

  int r = -1;
  ramdisk_client_t* ramdisk = NULL;
  int fd;
  if (!strcmp(argv[3], "--ramdisk")) {
    if (strcmp(argv[2], "block")) {
      fprintf(stderr, "ramdisk only supported for block\n");
      goto done;
    }
    zx_status_t status = ramdisk_create(512, total / 512, &ramdisk);
    if (status != ZX_OK) {
      fprintf(stderr, "error: cannot create %zu-byte ramdisk\n", total);
      goto done;
    }
    fd = ramdisk_get_block_fd(ramdisk);
  } else {
    if ((fd = open(argv[3], is_read ? O_RDONLY : O_WRONLY)) < 0) {
      fprintf(stderr, "error: cannot open '%s'\n", argv[3]);
      goto done;
    }
  }

  zx_duration_t res;
  if (!strcmp(argv[2], "posix")) {
    res = iotime_posix(is_read, fd, total, bufsz);
  } else if (!strcmp(argv[2], "block")) {
    res = iotime_block(is_read, fd, total, bufsz);
  } else if (!strcmp(argv[2], "fifo")) {
    res = iotime_fifo(argv[3], is_read, fd, total, bufsz);
  } else {
    fprintf(stderr, "error: unknown mode '%s'\n", argv[2]);
    goto done;
  }

  if (res != ZX_TIME_INFINITE) {
    fprintf(stderr, "%s %zu bytes in %zu ns: ", is_read ? "read" : "write", total, res);
    bytes_per_second(total, res);
    r = 0;
    goto done;
  } else {
    goto done;
  }

done:
  if (ramdisk != NULL) {
    ramdisk_destroy(ramdisk);
  }
  return r;
}
