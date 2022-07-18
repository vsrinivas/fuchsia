// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

void usage() {
  fprintf(stderr,
          "usage: dlog        dump the zircon debug log\n"
          "\n"
          "options: -f        don't exit, keep waiting for new messages\n"
          "         -p <pid>  only show messages from specified pid\n"
          "         -t        only show the text of messages (no metadata)\n"
          "         -h        show help\n");
}

int main(int argc, char** argv) {
  bool tail = false;
  bool filter_pid = false;
  bool plain = false;
  zx_koid_t pid = 0;

  while (argc > 1) {
    if (!strcmp(argv[1], "-h")) {
      usage();
      return 0;
    }
    if (!strcmp(argv[1], "-f")) {
      tail = true;
    } else if (!strcmp(argv[1], "-t")) {
      plain = true;
    } else if (!strcmp(argv[1], "-p")) {
      argc--;
      argv++;
      if (argc < 2) {
        usage();
        return -1;
      }
      errno = 0;
      pid = strtoull(argv[1], nullptr, 0);
      if (errno) {
        fprintf(stderr, "dlog: invalid pid\n");
        return -1;
      }
      filter_pid = true;
    } else {
      usage();
      return -1;
    }
    argc--;
    argv++;
  }

  zx::status client = service::Connect<fuchsia_boot::ReadOnlyLog>();
  if (client.is_error()) {
    fprintf(stderr, "failed to connect to read only log: %s\n", client.status_string());
    return -1;
  }

  const fidl::WireResult result = fidl::WireCall(client.value())->Get();
  if (!result.ok()) {
    fprintf(stderr, "failed to get read only log handle: %s\n", result.status_string());
    return -1;
  }
  const zx::debuglog& log = result.value().log;

  alignas(zx_log_record_t) char buf[ZX_LOG_RECORD_MAX];
  for (;;) {
    // Per zx_debuglog_read:
    //
    //   The length of the record in bytes is given in the syscall's return
    //   value.
    zx_status_t status = log.read(0, buf, sizeof(buf));
    if (status < 0) {
      if (status == ZX_ERR_SHOULD_WAIT) {
        if (tail) {
          if (zx_status_t status = log.wait_one(ZX_LOG_READABLE, zx::time::infinite(), nullptr);
              status != ZX_OK) {
            fprintf(stderr, "failed to wait for read only log handle: %s\n",
                    zx_status_get_string(status));
            return -1;
          }
          continue;
        }
        break;
      }
      fprintf(stderr, "failed to read from read only log handle: %s\n",
              zx_status_get_string(status));
      return -1;
    }
    uint32_t read = static_cast<uint32_t>(status);
    ZX_ASSERT_MSG(read >= sizeof(zx_log_record_t), "read %d/%lu", read, sizeof(zx_log_record_t));
    ZX_ASSERT_MSG(read <= sizeof(buf), "read %d/%lu", read, sizeof(buf));
    zx_log_record_t& rec = *reinterpret_cast<zx_log_record_t*>(buf);
    ZX_ASSERT_MSG(read == sizeof(rec) + rec.datalen, "inconsistent read of %d with datalen %d",
                  read, rec.datalen);
    if (filter_pid && (pid != rec.pid)) {
      continue;
    }
    if (!plain) {
      char tmp[32];
      size_t len = snprintf(tmp, sizeof(tmp), "[%05llu.%03llu] ", rec.timestamp / 1000000000ULL,
                            (rec.timestamp / 1000000ULL) % 1000ULL);
      ssize_t ret = write(STDOUT_FILENO, tmp, len);
      if (ret < 0) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
        return -1;
      }
      size_t n = static_cast<size_t>(ret);
      if (n != len) {
        fprintf(stderr, "short write %zu/%zu\n", n, len);
        return -1;
      }
    }
    std::string_view line(rec.data, rec.datalen);
    ssize_t ret = write(STDOUT_FILENO, line.data(), line.size());
    if (ret < 0) {
      fprintf(stderr, "write failed: %s\n", strerror(errno));
      return -1;
    }
    size_t n = static_cast<size_t>(ret);
    if (n != line.size()) {
      fprintf(stderr, "short write %zu/%lu\n", n, line.size());
      return -1;
    }
    constexpr char endl[] = "\n";
    if (!cpp20::ends_with(line, endl)) {
      ssize_t ret = write(STDOUT_FILENO, endl, sizeof(endl));
      if (ret < 0) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
        return -1;
      }
      size_t n = static_cast<size_t>(ret);
      if (n != sizeof(endl)) {
        fprintf(stderr, "short write %zu/%lu\n", n, sizeof(endl));
        return -1;
      }
    }
  }
  return 0;
}
