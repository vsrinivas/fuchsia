// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
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
  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(bufsz, 0, &vmo); status != ZX_OK) {
    fprintf(stderr, "error: out of memory: %s\n", zx_status_get_string(status));
    return ZX_TIME_INFINITE;
  }

  fdio_cpp::UnownedFdioCaller disk_connection(fd);
  fidl::UnownedClientEnd channel = disk_connection.borrow_as<fuchsia_hardware_block::Block>();

  fuchsia_hardware_block::wire::BlockInfo info;
  {
    const fidl::WireResult result = fidl::WireCall(channel)->GetInfo();
    if (!result.ok()) {
      fprintf(stderr, "error: cannot get info for '%s':%s\n", dev,
              result.FormatDescription().c_str());
      return ZX_TIME_INFINITE;
    }
    const fidl::WireResponse response = result.value();
    if (zx_status_t status = response.status; status != ZX_OK) {
      fprintf(stderr, "error: cannot get info for '%s':%s\n", dev, zx_status_get_string(status));
      return ZX_TIME_INFINITE;
    }
    info = *response.info;
  }

  zx::fifo fifo;
  {
    fidl::WireResult result = fidl::WireCall(channel)->GetFifo();
    if (!result.ok()) {
      fprintf(stderr, "error: cannot get fifo for '%s':%s\n", dev,
              result.FormatDescription().c_str());
      return ZX_TIME_INFINITE;
    }
    auto& response = result.value();
    if (zx_status_t status = response.status; status != ZX_OK) {
      fprintf(stderr, "error: cannot get fifo for '%s':%s\n", dev, zx_status_get_string(status));
      return ZX_TIME_INFINITE;
    }
    fifo = std::move(response.fifo);
  }

  fuchsia_hardware_block::wire::VmoId vmoid;
  {
    zx::vmo dup;
    if (zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup); status != ZX_OK) {
      fprintf(stderr, "error: cannot duplicate handle: %s\n", zx_status_get_string(status));
      return ZX_TIME_INFINITE;
    }

    const fidl::WireResult result = fidl::WireCall(channel)->AttachVmo(std::move(dup));
    if (!result.ok()) {
      fprintf(stderr, "error: cannot attach vmo for '%s':%s\n", dev,
              result.FormatDescription().c_str());
      return ZX_TIME_INFINITE;
    }
    const fidl::WireResponse response = result.value();
    if (zx_status_t status = response.status; status != ZX_OK) {
      fprintf(stderr, "error: cannot attach vmo for '%s':%s\n", dev, zx_status_get_string(status));
      return ZX_TIME_INFINITE;
    }
    vmoid = *response.vmoid;
  }

  groupid_t group = 0;
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
    if (zx_status_t status = client.Transaction(&request, 1); status != ZX_OK) {
      fprintf(stderr, "error: block_fifo_txn error %s\n", zx_status_get_string(status));
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
      fprintf(stderr, "error: cannot create %zu-byte ramdisk: %s\n", total,
              zx_status_get_string(status));
      goto done;
    }
    zx_handle_t handle = ramdisk_get_block_interface(ramdisk);
    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    zx_handle_t cloned = fdio_service_clone(handle);
    if (zx_status_t status = fdio_fd_create(cloned, &fd); status != ZX_OK) {
      fprintf(stderr, "error: cannot create ramdisk fd: %s\n", zx_status_get_string(status));
      goto done;
    }
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
