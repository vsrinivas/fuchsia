// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_BUFFER_READER_H_
#define GARNET_LIB_PERFMON_BUFFER_READER_H_

#include <cstdint>
#include <memory>
#include <string>

#include <src/lib/fxl/macros.h>
#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <zircon/types.h>

#include "records.h"
#include "types.h"

namespace perfmon {

// This class provides support for reading one in-memory buffer of data.

class BufferReader {
 public:
  // |buffer| must be sufficiently aligned (uin64_t).
  static ReaderStatus Create(const std::string& name, const void* buffer,
                             size_t buffer_size,
                             std::unique_ptr<BufferReader>* out_reader);

  static ReaderStatus AnalyzeHeader(const perfmon_buffer_header_t* header,
                                    size_t buffer_size);

  ReaderStatus status() const { return status_; }

  // The returned value is zero until the first call to ReadNextRecord(),
  // after which it contains the value used by the trace.
  // Note: The returned value could be bogus, including zero.
  // We just pass on what the trace told us.
  uint64_t ticks_per_second() const { return ticks_per_second_; }

  // Return the current time, in ticks, based on the last time record read.
  // It is assumed that ReadNextRecord has been called at least once.
  // Returns zero if not.
  zx_time_t time() const { return time_; }

  // Return a pointer to the buffer we're reading.
  const void* buffer() const { return buffer_; }

  // Return the total number of bytes captured.
  size_t captured_bytes() const { return buffer_end_ - buffer_; }

  // Return the number of remaining bytes to be read.
  size_t remaining_bytes() const { return buffer_end_ - next_record_; }

  // Return the offset of the last record read, for error reporting purposes.
  // Only valid after a call to |ReadNextRecord()|.
  size_t last_record_offset() const { return last_record_ - buffer_; }

  // Read the next record.
  // Note: To avoid unnecessary copying of larger records, the result contains
  // a pointer to the record. Such pointers remain valid until the next call.
  // Returns true on success, false if there are no more records.
  // If an error is encountered during reading an error is logged and
  // false is returned.
  ReaderStatus ReadNextRecord(SampleRecord* record);

 private:
  // |buffer| must be sufficiently aligned (uin64_t).
  BufferReader(const std::string& name, const void* buffer,
               size_t capture_end);

  // Utility to update |status_| and return the current value.
  // The status is updated only if it is currently |kOk|.
  ReaderStatus set_status(ReaderStatus status) {
    if (status_ == ReaderStatus::kOk)
      status_ = status;
    return status_;
  }

  // The name of the buffer, used for logging/error reporting.
  const std::string name_;
  const uint8_t* const buffer_;

  const perfmon_buffer_header_t* const header_;
  const uint8_t* next_record_ = nullptr;
  const uint8_t* last_record_ = nullptr;
  const uint8_t* buffer_end_ = nullptr;

  // Reading of one trace can span multiple cpus, and the ticks-per-second
  // value comes from each cpu's trace. Generally it's all the same value,
  // but there is no uber record to specify that. zx_ticks_per_second() will
  // return a constant value (though not necessarily the same value on each
  // boot), and it's this value we expect in the trace. OTOH, we use what
  // the trace buffer gives us. We don't want each record to encode its own
  // value, so keep track of the value here.
  uint64_t ticks_per_second_;

  // The time from the last PERFMON_RECORD_TIME record read.
  zx_time_t time_ = 0;

  // Reader status. Once we get a reader error, reading stops.
  ReaderStatus status_ = ReaderStatus::kOk;

  FXL_DISALLOW_COPY_AND_ASSIGN(BufferReader);
};

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_BUFFER_READER_H_
