// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cpuperf/reader.h"

#include <inttypes.h>

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace cpuperf {

Reader::Reader(int fd, uint32_t buffer_size)
    : fd_(fd), buffer_size_(buffer_size), num_cpus_(zx_system_get_num_cpus()) {
  FXL_DCHECK(fd_ >= 0);
  uintptr_t addr;
  auto status = zx::vmar::root_self()->allocate(
      0u, buffer_size_, ZX_VM_FLAG_CAN_MAP_READ, &vmar_, &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to obtain vmar for reading trace data: "
                   << status;
  }
}

bool Reader::GetProperties(cpuperf_properties_t* props) {
  auto status = ioctl_cpuperf_get_properties(fd_, props);
  if (status < 0)
    FXL_LOG(ERROR) << "ioctl_cpuperf_get_properties failed: " << status;
  return status >= 0;
}

bool Reader::GetConfig(cpuperf_config_t* config) {
  auto status = ioctl_cpuperf_get_config(fd_, config);
  if (status < 0)
    FXL_LOG(ERROR) << "ioctl_cpuperf_get_config failed: " << status;
  return status >= 0;
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

  auto status =
      vmar_.map(0, current_vmo_, 0, buffer_size_, ZX_VM_FLAG_PERM_READ, &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map buffer vmo: " << status;
    return false;
  }

  buffer_start_ = reinterpret_cast<const uint8_t*>(addr);
  return true;
}

static bool ReadBufferHeader(const void* buffer, uint32_t cpu,
                             cpuperf_buffer_header_t* hdr) {
  memcpy(hdr, buffer, sizeof(*hdr));

  FXL_LOG(INFO) << "cpu " << cpu << ": buffer version " << hdr->version << ", "
                << hdr->capture_end << " bytes";

  uint32_t expected_version = CPUPERF_BUFFER_VERSION;
  if (hdr->version != expected_version) {
    FXL_LOG(ERROR) << "Unsupported buffer version, got " << hdr->version
                   << " instead of " << expected_version;
    return false;
  }

  uint64_t kernel_ticks_per_second = hdr->ticks_per_second;
  uint64_t user_ticks_per_second = zx_ticks_per_second();
  if (kernel_ticks_per_second != user_ticks_per_second) {
    FXL_LOG(WARNING) << "Kernel and userspace are using different tracing"
                     << " timebases, tracks may be misaligned:"
                     << " kernel_ticks_per_second=" << kernel_ticks_per_second
                     << " user_ticks_per_second=" << user_ticks_per_second;
  }

  return true;
}

bool Reader::ReadNextRecord(uint32_t* cpu, SampleRecord* record) {
  while (current_cpu_ < num_cpus_) {
    // If this is the first cpu, or if we're done with this cpu's records,
    // move to the next cpu.
    if (next_record_ == nullptr || next_record_ >= capture_end_) {
      if (next_record_ != nullptr) ++current_cpu_;
      if (current_cpu_ >= num_cpus_) break;
      ioctl_cpuperf_buffer_handle_req_t req;
      req.descriptor = current_cpu_;
      zx_handle_t handle;
      auto ioctl_status = ioctl_cpuperf_get_buffer_handle(fd_, &req, &handle);
      if (ioctl_status < 0) {
        FXL_LOG(ERROR) << "ioctl_cpuperf_get_buffer_handle failed: "
                       << ioctl_status;
        return false;
      }

      // Out with the old, in with the new.
      if (!MapBufferVmo(handle)) return false;

      cpuperf_buffer_header_t header;
      if (!ReadBufferHeader(buffer_start_, current_cpu_, &header)) return false;
      next_record_ = buffer_start_ + sizeof(header);
      capture_end_ = buffer_start_ + header.capture_end;
      ticks_per_second_ = header.ticks_per_second;
      if (next_record_ > capture_end_) {
        FXL_LOG(WARNING) << "Bad trace data for cpu " << current_cpu_
                         << ", end point within header";
        continue;
      }
      if (next_record_ == capture_end_) continue;
    }

    const cpuperf_record_header_t* hdr =
        reinterpret_cast<const cpuperf_record_header_t*>(next_record_);
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
                       << ", bad record type: "
                       << static_cast<unsigned>(hdr->type);
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

    // There can be millions of records. This is useful for small test runs,
    // but otherwise is too painful. The verbosity level is chosen to
    // recognize that.
    FXL_VLOG(10) << fxl::StringPrintf("ReadNextRecord: cpu=%u, offset=%zu",
                                      current_cpu_,
                                      next_record_ - buffer_start_);

    switch (record_type) {
      case CPUPERF_RECORD_TIME:
        record->time =
            reinterpret_cast<const cpuperf_time_record_t*>(next_record_);
        time_ = record->time->time;
        break;
      case CPUPERF_RECORD_TICK:
        record->tick =
            reinterpret_cast<const cpuperf_tick_record_t*>(next_record_);
        break;
      case CPUPERF_RECORD_COUNT:
        record->count =
            reinterpret_cast<const cpuperf_count_record_t*>(next_record_);
        break;
      case CPUPERF_RECORD_VALUE:
        record->value =
            reinterpret_cast<const cpuperf_value_record_t*>(next_record_);
        break;
      case CPUPERF_RECORD_PC:
        record->pc = reinterpret_cast<const cpuperf_pc_record_t*>(next_record_);
        break;
      default:
        // We shouldn't get here because RecordSize() should have returned
        // zero and we would have skipped to the next cpu.
        FXL_NOTREACHED();
    }

    next_record_ += record_size;
    *cpu = current_cpu_;
    return true;
  }

  return false;
}

cpuperf_record_type_t Reader::RecordType(const cpuperf_record_header_t* hdr) {
  switch (hdr->type) {
    case CPUPERF_RECORD_TIME:
    case CPUPERF_RECORD_TICK:
    case CPUPERF_RECORD_COUNT:
    case CPUPERF_RECORD_VALUE:
    case CPUPERF_RECORD_PC:
      return static_cast<cpuperf_record_type_t>(hdr->type);
    default:
      return CPUPERF_RECORD_RESERVED;
  }
}

size_t Reader::RecordSize(const cpuperf_record_header_t* hdr) {
  switch (hdr->type) {
    case CPUPERF_RECORD_TIME:
      return sizeof(cpuperf_time_record_t);
    case CPUPERF_RECORD_TICK:
      return sizeof(cpuperf_tick_record_t);
    case CPUPERF_RECORD_COUNT:
      return sizeof(cpuperf_count_record_t);
    case CPUPERF_RECORD_VALUE:
      return sizeof(cpuperf_value_record_t);
    case CPUPERF_RECORD_PC:
      return sizeof(cpuperf_pc_record_t);
    default:
      return 0;
  }
}

}  // namespace cpuperf
