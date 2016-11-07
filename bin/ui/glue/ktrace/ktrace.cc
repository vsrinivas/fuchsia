// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/glue/ktrace/ktrace.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <mutex>

#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "magenta/device/ktrace.h"

namespace ktrace {
namespace {

class Tracer {
 public:
  Tracer(int fd, mx_handle_t handle) : fd_(fd), handle_(handle) {}

  ~Tracer() {
    close(fd_);
    mx_handle_close(handle_);
  }

  static Tracer* GetInstance() {
    std::call_once(instance_initialized_, [] {
      int fd;
      if ((fd = open("/dev/class/misc/ktrace", O_RDWR)) < 0) {
        FTL_LOG(ERROR) << "Failed to open ktrace driver: errno=" << errno;
        return;
      }

      mx_status_t status;
      mx_handle_t handle;
      if ((status = ioctl_ktrace_get_handle(fd, &handle)) < 0) {
        FTL_LOG(ERROR) << "Failed to get ktrace pipe handle: status=" << status;
        close(fd);
        return;
      }

      instance_ = new Tracer(fd, handle);
    });

    return instance_;
  }

  uint32_t AddProbe(const char* name) {
    // Truncate to max name length, preserving end of string if too long.
    size_t len = strlen(name) + 1;
    if (len > MX_MAX_NAME_LEN)
      name += len - MX_MAX_NAME_LEN;

    mx_status_t status;
    uint32_t probe_id = 0u;
    if ((status = ioctl_ktrace_add_probe(fd_, name, &probe_id)) < 0) {
      FTL_LOG(ERROR) << "Failed to add ktrace probe: name=" << name
                     << ", status=" << status;
      return 0u;
    }
    return probe_id;
  }

  void WriteProbe(uint32_t probe_id, uint32_t arg1, uint32_t arg2) {
    mx_ktrace_write(handle_, probe_id, arg1, arg2);
  }

 private:
  static std::once_flag instance_initialized_;
  static Tracer* instance_;

  int fd_;
  mx_handle_t handle_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Tracer);
};

std::once_flag Tracer::instance_initialized_;
Tracer* Tracer::instance_;

}  // namespace

uint32_t TraceAddProbe(const char* name) {
  Tracer* tracer = Tracer::GetInstance();
  return tracer ? tracer->AddProbe(name) : 0u;
}

void TraceWriteProbe(uint32_t probe_id, uint32_t arg1, uint32_t arg2) {
  Tracer* tracer = Tracer::GetInstance();
  if (tracer)
    tracer->WriteProbe(probe_id, arg1, arg2);
}

}  // namespace ktrace
