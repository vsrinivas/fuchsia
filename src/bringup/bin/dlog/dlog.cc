// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>
#include <unistd.h>
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

  zx::status client = component::Connect<fuchsia_boot::ReadOnlyLog>();
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
    // Calculate needed capacity for the log line.
    constexpr size_t ts_len = 1 + 5 + 1 + 3 + 2;
    constexpr char endl = '\n';
    const bool has_newline = rec.datalen != 0 && rec.data[rec.datalen - sizeof(endl)] == endl;
    const size_t cap = [plain, has_newline, datalen = rec.datalen]() {
      const size_t ts_cap = !plain ? ts_len : 0;
      const size_t newline_cap = !has_newline ? sizeof(endl) : 0;
      return ts_cap + datalen + newline_cap;
    }();
    char buf[cap];
    size_t len = 0;
    if (!plain) {
      const zx_time_t millis = rec.timestamp / 1000000;
      const zx_time_t secs = millis / 1000;
      const zx_time_t ms = millis % 1000;
      int ret = snprintf(&buf[len], sizeof(buf) - len, "[%05ld.%03ld] ", secs, ms);
      ZX_ASSERT_MSG(ret >= 0, "snprintf failed: %d", ret);
      size_t n = static_cast<size_t>(ret);
      ZX_ASSERT_MSG(n == ts_len, "unexpected timestamp length %zu/%zu", n, ts_len);
      len += n;
    }
    memcpy(&buf[len], rec.data, rec.datalen);
    len += rec.datalen;
    if (!has_newline) {
      memcpy(&buf[len], &endl, sizeof(endl));
      len += sizeof(endl);
    }

    std::string_view view(buf, len);
    while (!view.empty()) {
      ssize_t ret = write(STDOUT_FILENO, view.data(), view.size());
      if (ret < 0) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
        return -1;
      }
      size_t n = static_cast<size_t>(ret);
      view = view.substr(n);
    }
  }
  return 0;
}
