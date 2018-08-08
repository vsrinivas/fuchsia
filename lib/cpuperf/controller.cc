// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cpuperf/controller.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>

#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace cpuperf {

const char kCpuPerfDev[] = "/dev/sys/cpu-trace/cpu-trace";

static bool IsSampleMode(const cpuperf_config_t& config) {
  for (size_t i = 0; i < countof(config.rate); ++i) {
    // If any event is doing sampling, then we're in "sample mode".
    if (config.rate[i] != 0) {
      return true;
    }
  }
  return false;
}

static uint32_t GetBufferSize(bool sample_mode, uint32_t requested_size_in_mb) {
  if (sample_mode)
    return requested_size_in_mb * 1024 * 1024;
  // For "counting mode" we just need something large enough to hold
  // the header + records for each event.
  unsigned num_events = CPUPERF_MAX_EVENTS;
  uint32_t size = (sizeof(cpuperf_buffer_header_t) +
                   num_events * sizeof(cpuperf_value_record_t));
  return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

Controller::Controller(uint32_t buffer_size_in_mb,
                       const cpuperf_config_t& config)
    : sample_mode_(IsSampleMode(config)),
      buffer_size_(GetBufferSize(sample_mode_, buffer_size_in_mb)),
      config_(config),
      alloc_(false),
      started_(false) {
  int fd = open(kCpuPerfDev, O_WRONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open " << kCpuPerfDev << ": errno=" << errno;
    return;
  }
  fd_.reset(fd);

  Alloc();
  if (!alloc_)
    fd_.reset();
}

Controller::~Controller() {
  Stop();
  Free();
}

bool Controller::is_valid() const { return fd_.is_valid() && alloc_; }

bool Controller::Start() {
  if (!is_valid()) {
    return false;
  }
  if (started_) {
    FXL_LOG(ERROR) << "already started";
    return false;
  }

  if (!Stage()) {
    return false;
  }

  auto status = ioctl_cpuperf_start(fd_.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ioctl_cpuperf_start failed: status=" << status;
  } else {
    started_ = true;
  }
  return status == ZX_OK;
}

void Controller::Stop() {
  if (!is_valid()) {
    return;
  }
  auto status = ioctl_cpuperf_stop(fd_.get());
  if (status != ZX_OK) {
    // This can get called while tracing is currently stopped.
    if (!started_ && status == ZX_ERR_BAD_STATE) {
      ;  // dont report an error in this case
    } else {
      FXL_LOG(ERROR) << "ioctl_cpuperf_stop failed: status=" << status;
    }
  } else {
    started_ = false;
  }
}

void Controller::Alloc() {
  FXL_DCHECK(!alloc_ && !started_);
  ioctl_cpuperf_alloc_t alloc;
  alloc.num_buffers = zx_system_get_num_cpus();
  alloc.buffer_size = buffer_size_;
  FXL_VLOG(2) << fxl::StringPrintf("num_buffers=%u, buffer_size=0x%x",
                                   alloc.num_buffers, alloc.buffer_size);
  auto status = ioctl_cpuperf_alloc_trace(fd_.get(), &alloc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ioctl_cpuperf_alloc_trace failed: status=" << status;
  } else {
    alloc_ = true;
  }
}

bool Controller::Stage() {
  FXL_DCHECK(is_valid() && !started_);
  auto status = ioctl_cpuperf_stage_config(fd_.get(), &config_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ioctl_cpuperf_stage_config failed: status=" << status;
  }
  return status == ZX_OK;
}

void Controller::Free() {
  if (!is_valid()) {
    return;
  }
  auto status = ioctl_cpuperf_free_trace(fd_.get());
  if (status != ZX_OK) {
    // This can get called while tracing is currently stopped.
    if (!started_ && status == ZX_ERR_BAD_STATE) {
      ;  // dont report an error in this case
    } else {
      FXL_LOG(ERROR) << "ioctl_cpuperf_free_trace failed: status=" << status;
    }
  }
}

std::unique_ptr<Reader> Controller::GetReader() {
  return std::unique_ptr<Reader>(new Reader(fd_.get(), buffer_size_));
}

}  // namespace cpuperf
