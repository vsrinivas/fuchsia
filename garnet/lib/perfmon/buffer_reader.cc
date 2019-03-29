// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

#include <src/lib/fxl/logging.h>

#include "buffer_reader.h"
#include "records.h"

namespace perfmon {

ReaderStatus BufferReader::Create(const std::string& name, const void* buffer,
                                  size_t buffer_size,
                                  std::unique_ptr<BufferReader>* out_reader) {
  auto header = reinterpret_cast<const perfmon_buffer_header_t*>(buffer);
  ReaderStatus status = AnalyzeHeader(header, buffer_size);
  if (status != ReaderStatus::kOk) {
    return status;
  }
  out_reader->reset(new BufferReader(name, buffer, header->capture_end));
  return ReaderStatus::kOk;
}

BufferReader::BufferReader(const std::string& name, const void* buffer,
                           size_t capture_end)
    : name_(name),
      buffer_(reinterpret_cast<const uint8_t*>(buffer)),
      header_(reinterpret_cast<const perfmon_buffer_header_t*>(buffer)),
      next_record_(buffer_ + sizeof(*header_)),
      buffer_end_(buffer_ + capture_end),
      ticks_per_second_(header_->ticks_per_second) {
}

ReaderStatus BufferReader::AnalyzeHeader(const perfmon_buffer_header_t* header,
                                         size_t buffer_size) {
  FXL_VLOG(2) << "Reading header, buffer version "
              << header->version << ", " << header->capture_end << " bytes";

  // TODO(dje): check magic

  uint32_t expected_version = PERFMON_BUFFER_VERSION;
  if (header->version != expected_version) {
    FXL_LOG(ERROR) << "Unsupported buffer version, got " << header->version
                   << " instead of " << expected_version;
    return ReaderStatus::kHeaderError;
  }

  if (header->capture_end > buffer_size) {
    FXL_LOG(ERROR) << "Bad trace data, end point beyond buffer";
    return ReaderStatus::kHeaderError;
  }
  if (header->capture_end < sizeof(*header)) {
    FXL_LOG(ERROR) << "Bad trace data, end point within header";
    return ReaderStatus::kHeaderError;
  }

#ifdef __Fuchsia__
  uint64_t user_ticks_per_second = zx_ticks_per_second();
  if (header->ticks_per_second != user_ticks_per_second) {
    FXL_LOG(WARNING) << "Kernel and userspace are using different tracing"
                     << " timebases, tracks may be misaligned:"
                     << " kernel_ticks_per_second=" << header->ticks_per_second
                     << " user_ticks_per_second=" << user_ticks_per_second;
  }
#endif

  return ReaderStatus::kOk;
}

ReaderStatus BufferReader::ReadNextRecord(SampleRecord* record) {
  if (status_ != ReaderStatus::kOk)
    return status_;

  if (next_record_ >= buffer_end_) {
    return set_status(ReaderStatus::kNoMoreRecords);
  }

  const perfmon_record_header_t* hdr =
    reinterpret_cast<const perfmon_record_header_t*>(next_record_);
  if (next_record_ + sizeof(*hdr) > buffer_end_) {
    FXL_LOG(ERROR) << name_ << ": Bad trace data"
                   << ", no space for final record header";
    return set_status(ReaderStatus::kRecordError);
  }

  auto record_type = RecordType(hdr);
  auto record_size = RecordSize(hdr);
  if (record_size == 0) {
    FXL_LOG(ERROR) << name_ << ": Bad trace data, bad record type: "
                   << static_cast<unsigned>(hdr->type);
    return set_status(ReaderStatus::kRecordError);
  }
  if (next_record_ + record_size > buffer_end_) {
    FXL_LOG(ERROR) << name_ << ": Bad trace data"
                   << ", no space for final record";
    return set_status(ReaderStatus::kRecordError);
  }

  // There can be millions of records. This is useful for small test runs,
  // but otherwise is too painful. The verbosity level is chosen to
  // recognize that.
  FXL_VLOG(10) << "ReadNextRecord: offset=" << (next_record_ - buffer_);

  switch (record_type) {
  case PERFMON_RECORD_TIME:
    record->time =
      reinterpret_cast<const perfmon_time_record_t*>(next_record_);
    time_ = record->time->time;
    break;
  case PERFMON_RECORD_TICK:
    record->tick =
      reinterpret_cast<const perfmon_tick_record_t*>(next_record_);
    break;
  case PERFMON_RECORD_COUNT:
    record->count =
      reinterpret_cast<const perfmon_count_record_t*>(next_record_);
    break;
  case PERFMON_RECORD_VALUE:
    record->value =
      reinterpret_cast<const perfmon_value_record_t*>(next_record_);
    break;
  case PERFMON_RECORD_PC:
    record->pc = reinterpret_cast<const perfmon_pc_record_t*>(next_record_);
    break;
  case PERFMON_RECORD_LAST_BRANCH:
    record->last_branch =
      reinterpret_cast<const perfmon_last_branch_record_t*>(next_record_);
    break;
  default:
    // We shouldn't get here because RecordSize() should have returned
    // zero and we would have skipped to the next cpu.
    __UNREACHABLE;
  }

  last_record_ = next_record_;
  next_record_ += record_size;
  return ReaderStatus::kOk;
}

}  // namespace perfmon
