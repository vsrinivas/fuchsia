// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/reader.h"

#include <inttypes.h>

#include <zircon/syscalls.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace cpuperf_provider {
namespace {

}  // namespace

Reader::Reader(int fd, uint32_t buffer_size)
    : fd_(fd),
      buffer_size_(buffer_size),
      num_cpus_(zx_system_get_num_cpus()) {
  FXL_DCHECK(fd_ >= 0);
  uintptr_t addr;
  auto status = zx::vmar::root_self().allocate(0u, buffer_size_,
                                               ZX_VM_FLAG_CAN_MAP_READ,
                                               &vmar_, &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to obtain vmar for reading trace data: " << status;
  }
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

bool Reader::MapBufferVmo(zx_handle_t vmo) {
  uintptr_t addr;

  current_vmo_.reset(vmo);

  if (buffer_start_) {
    addr = reinterpret_cast<uintptr_t>(buffer_start_);
    auto status = vmar_.unmap(addr, buffer_size_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Unable to unmap previous buffer vmo: " << status;
      return false;
    }
  }

  auto status = vmar_.map(0, current_vmo_, 0, buffer_size_,
                          ZX_VM_FLAG_PERM_READ, &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map buffer vmo: " << status;
    return false;
  }

  buffer_start_ = reinterpret_cast<const uint8_t*>(addr);
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
                            SampleRecord* record) {
  while (current_cpu_ < num_cpus_) {
    // If this is the first cpu, or if we're done with this cpu's records,
    // move to the next cpu.
    if (next_record_ == nullptr || next_record_ >= capture_end_) {
      if (next_record_ != nullptr)
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

      // Out with the old, in with the new.
      if (!MapBufferVmo(handle))
        return false;

      zx_x86_ipm_buffer_info_t info;
      if (!ReadBufferInfo(current_vmo_, current_cpu_, true, &info))
        return false;
      next_record_ = buffer_start_ + sizeof(info);
      capture_end_ = buffer_start_ + info.capture_end;
      ticks_per_second_ = info.ticks_per_second;
      if (next_record_ > capture_end_) {
        FXL_LOG(WARNING) << "Bad trace data for cpu " << current_cpu_
                         << ", end point within header";
        continue;
      }
      if (next_record_ == capture_end_)
        continue;
    }

    const zx_x86_ipm_record_header_t* hdr =
      reinterpret_cast<const zx_x86_ipm_record_header_t*>(next_record_);
    if (next_record_ + sizeof(*hdr) > capture_end_) {
      FXL_LOG(WARNING) << "Bad trace data for cpu " << current_cpu_
                       << ", no space for final record header";
      // Bump |next_record_| so that we'll skip to the next cpu.
      next_record_ = capture_end_;
      continue;
    }
    auto record_type = RecordType(hdr);
    auto record_size = RecordSize(hdr);
    if (record_size == 0) {
      FXL_LOG(WARNING) << "Bad trace data for cpu " << current_cpu_
                       << ", bad record type: " << hdr->type;
      // Bump |next_record_| so that we'll skip to the next cpu.
      next_record_ = capture_end_;
      continue;
    }
    if (next_record_ + record_size > capture_end_) {
      FXL_LOG(WARNING) << "Bad trace data for cpu " << current_cpu_
                       << ", no space for final record";
      // Bump |next_record_| so that we'll skip to the next cpu.
      next_record_ = capture_end_;
      continue;
    }

    FXL_VLOG(2) << fxl::StringPrintf("ReadNextRecord: cpu=%u, offset=%zu",
                                     current_cpu_,
                                     next_record_ - buffer_start_);

    switch (record_type) {
#if IPM_API_VERSION >= 2
      case IPM_RECORD_TICK:
        memcpy(&record->tick, next_record_, sizeof(record->tick));
        break;
      case IPM_RECORD_PC:
        memcpy(&record->pc, next_record_, sizeof(record->pc));
        break;
#endif
      default:
        FXL_NOTREACHED();
    }

    next_record_ += record_size;
    *cpu = current_cpu_;
    *ticks_per_second = ticks_per_second_;
    return true;
  }

  return false;
}

zx_x86_ipm_record_type_t Reader::RecordType(
    const zx_x86_ipm_record_header_t* hdr) {
  switch (hdr->type) {
    case IPM_RECORD_TICK:
      return IPM_RECORD_TICK;
    case IPM_RECORD_PC:
      return IPM_RECORD_PC;
    default:
      return IPM_RECORD_RESERVED;
  }
}

size_t Reader::RecordSize(const zx_x86_ipm_record_header_t* hdr) {
  switch (hdr->type) {
#if IPM_API_VERSION >= 2
    case IPM_RECORD_TICK:
      return sizeof(zx_x86_ipm_tick_record_t);
    case IPM_RECORD_PC:
      return sizeof(zx_x86_ipm_pc_record_t);
#endif
    default:
      return 0;
  }
}

}  // namespace cpuperf_provider
