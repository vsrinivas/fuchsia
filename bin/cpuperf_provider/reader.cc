// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/reader.h"

#include <inttypes.h>

#include <zircon/syscalls.h>
#include <zx/vmo.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace cpuperf_provider {
namespace {

}  // namespace

Reader::Reader(int fd)
    : fd_(fd),
      num_cpus_(zx_system_get_num_cpus()) {
  FXL_DCHECK(fd_ >= 0);
}

bool Reader::ReadState(zx_x86_ipm_state_t* state) {
  auto status = ioctl_ipm_get_state(fd_, state);
  if (status < 0)
    FXL_LOG(ERROR) << "ioctl_ipm_get_state failed: " << status;
  return status >= 0;
}

bool Reader::ReadPerfConfig(zx_x86_ipm_perf_config_t* config) {
  ioctl_ipm_perf_config_t ioctl_config;
  auto status = ioctl_ipm_get_perf_config(fd_, &ioctl_config);
  if (status < 0) {
    FXL_LOG(ERROR) << "ioctl_ipm_get_perf_config failed: " << status;
    return false;
  }
  *config = ioctl_config.config;
  return true;
}

static bool ReadBufferInfo(zx::vmo& vmo, uint32_t cpu, bool sampling_mode,
                           zx_x86_ipm_buffer_info_t* info) {
  size_t actual;
  auto status = vmo.read(info, 0, sizeof(*info), &actual);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_vmo_read failed: " << status;
    return false;
  }
  if (actual != sizeof(*info)) {
    FXL_LOG(ERROR) << "zx_vmo_read short read, got " << actual
                   << " instead of " << sizeof(info);
    return false;
  }

  FXL_LOG(INFO) << "cpu " << cpu
                << ": buffer version " << info->version
                << ", " << info->capture_end << " bytes";

  uint32_t expected_version =
    sampling_mode ?
    IPM_BUFFER_SAMPLING_MODE_VERSION :
    IPM_BUFFER_COUNTING_MODE_VERSION;
  if (info->version != expected_version) {
    FXL_LOG(ERROR) << "Unsupported buffer version, got " << info->version
                   << " instead of " << expected_version;
    return false;
  }

  uint64_t kernel_ticks_per_second = info->ticks_per_second;
  uint64_t user_ticks_per_second = zx_ticks_per_second();
  if (kernel_ticks_per_second != user_ticks_per_second) {
    FXL_LOG(WARNING) << "Kernel and userspace are using different tracing"
                     << " timebases, tracks may be misaligned:"
                     << " kernel_ticks_per_second=" << kernel_ticks_per_second
                     << " user_ticks_per_second=" << user_ticks_per_second;
  }

  return true;
}

bool Reader::ReadNextRecord(uint32_t* cpu, zx_x86_ipm_counters_t* counters) {
  if (current_cpu_ >= num_cpus_)
    return false;

  ioctl_ipm_buffer_handle_req_t req;
  req.descriptor = current_cpu_;
  zx_handle_t handle;
  auto ioctl_status = ioctl_ipm_get_buffer_handle(fd_, &req, &handle);
  if (ioctl_status < 0) {
    FXL_LOG(ERROR) << "ioctl_ipm_get_buffer_handle failed: " << ioctl_status;
    return false;
  }

  zx::vmo vmo(handle);
  zx_x86_ipm_buffer_info_t info;
  if (!ReadBufferInfo(vmo, current_cpu_, false, &info))
    return false;

  FXL_VLOG(2) << fxl::StringPrintf("ReadNextRecord: cpu=%u", current_cpu_);

  size_t actual;
  auto status = vmo.read(counters, sizeof(info), sizeof(*counters), &actual);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_vmo_read failed: " << status;
    return false;
  }
  if (actual != sizeof(*counters)) {
    FXL_LOG(ERROR) << "zx_vmo_read short read, got " << actual
                   << " instead of " << sizeof(*counters);
    return false;
  }

  if (ticks_per_second_ != 0 &&
      ticks_per_second_ != info.ticks_per_second) {
    FXL_LOG(WARNING) << "Current buffer using different timebase from previous buffer"
                     << ": was " << ticks_per_second_
                     << " now " << info.ticks_per_second;
  }
  ticks_per_second_ = info.ticks_per_second;
  *cpu = current_cpu_;
  ++current_cpu_;

  return true;
}

bool Reader::ReadNextRecord(uint32_t* cpu, uint64_t* ticks_per_second,
                            zx_x86_ipm_sample_record_t* record) {
  while (current_cpu_ < num_cpus_) {
    // If this is the first cpu, or if we're done with this cpu's records,
    // move to the next cpu.
    if (next_record_ == 0 || next_record_ >= capture_end_) {
      if (next_record_ != 0)
        ++current_cpu_;
      if (current_cpu_ >= num_cpus_)
        break;
      ioctl_ipm_buffer_handle_req_t req;
      req.descriptor = current_cpu_;
      zx_handle_t handle;
      auto ioctl_status = ioctl_ipm_get_buffer_handle(fd_, &req, &handle);
      if (ioctl_status < 0) {
        FXL_LOG(ERROR) << "ioctl_ipm_get_buffer_handle failed: " << ioctl_status;
        return false;
      }
      current_vmo_.reset(handle);

      zx_x86_ipm_buffer_info_t info;
      if (!ReadBufferInfo(current_vmo_, current_cpu_, true, &info))
        return false;
      next_record_ = sizeof(info);
      capture_end_ = info.capture_end;
      ticks_per_second_ = info.ticks_per_second;
      if (next_record_ > capture_end_) {
        FXL_LOG(WARNING) << "Bad trace data for cpu " << current_cpu_
                         << ", end point within header";
        continue;
      }
      if (next_record_ == capture_end_)
        continue;
    }

    if (next_record_ + sizeof(*record) > capture_end_) {
      FXL_LOG(WARNING) << "Bad trace data for cpu " << current_cpu_
                       << ", end point not on record boundary";
      next_record_ += sizeof(*record);
      continue;
    }

    FXL_VLOG(2) << fxl::StringPrintf("ReadNextRecord: cpu=%u, offset=%" PRIu64,
                                     current_cpu_, next_record_);

    // TODO(dje): Maybe later map the vmo in.
    size_t actual;
    auto status = current_vmo_.read(record, next_record_, sizeof(*record), &actual);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx_vmo_read failed: " << status;
      return false;
    }
    if (actual != sizeof(*record)) {
      FXL_LOG(ERROR) << "zx_vmo_read short read, got " << actual
                     << " instead of " << sizeof(*record);
      return false;
    }

    next_record_ += sizeof(*record);
    *cpu = current_cpu_;
    *ticks_per_second = ticks_per_second_;
    return true;
  }

  return false;
}

}  // namespace cpuperf_provider
