// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_READER_H_
#define GARNET_LIB_PERFMON_READER_H_

#include <cstdint>
#include <memory>

#include <src/lib/fxl/macros.h>
#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <zircon/types.h>

#include "buffer_reader.h"

namespace perfmon {

class Reader {
 public:
  virtual ~Reader();

  uint32_t num_traces() const { return num_traces_; }

  // See |BufferReader::ticks_per_second()|.
  uint64_t ticks_per_second() const {
    if (buffer_reader_)
      return buffer_reader_->ticks_per_second();
    return 0;
  }

  // See |BufferReader::time()|.
  zx_time_t time() const {
    if (buffer_reader_)
      return buffer_reader_->time();
    return 0;
  }

  // Set the buffer we're reading to |trace_num|.
  ReaderStatus SetTrace(uint32_t trace_num);

  // Return a pointer to the current trace.
  // Returns nullptr if no buffer has been mapped yet.
  const void* GetCurrentTraceBuffer() const;

  // Return the size in bytes of the current trace.
  // Returns zero if no buffer has been mapped yet.
  size_t GetCurrentTraceSize() const;

  // Return the offset of the last record read, for error reporting purposes.
  // Only valid after a call to |ReadNextRecord()|.
  size_t GetLastRecordOffset() const;

  // Read the next record.
  // Note: To avoid unnecessary copying of larger records, the result contains
  // a pointer to the record. Such pointers remain valid until the next call.
  ReaderStatus ReadNextRecord(uint32_t* trace_num, SampleRecord* record);

 protected:
  Reader(uint32_t num_traces);

  virtual bool MapBuffer(const std::string& name, uint32_t trace_num) = 0;
  virtual bool UnmapBuffer() = 0;

  bool BufferMapped() const { return !!buffer_reader_; }

  // Utility to update |status_| and return the current value.
  // The status is updated only if it is currently |kOk|.
  ReaderStatus set_status(ReaderStatus status) {
    if (status_ == ReaderStatus::kOk)
      status_ = status;
    return status_;
  }

  const uint32_t num_traces_;
  uint32_t current_trace_ = 0;

  std::unique_ptr<BufferReader> buffer_reader_;

  // Reader status. Once we get a reader error, reading stops.
  ReaderStatus status_ = ReaderStatus::kOk;

  FXL_DISALLOW_COPY_AND_ASSIGN(Reader);
};

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_READER_H_
