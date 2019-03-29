// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/lib/perfmon/reader.h"

namespace perfmon {

Reader::Reader(uint32_t num_traces)
    : num_traces_(num_traces) {
}

Reader::~Reader() {
}

ReaderStatus Reader::SetTrace(uint32_t trace_num) {
  if (trace_num >= num_traces_) {
    FXL_LOG(ERROR) << "Bad trace number: " << trace_num;
    return ReaderStatus::kInvalidArgs;
  }

  // This wipes out any previous errors.
  status_ = ReaderStatus::kOk;
  if (!BufferMapped() || trace_num != current_trace_) {
    std::string name = fxl::StringPrintf("trace%u buffer", trace_num);
    if (!MapBuffer(name, trace_num)) {
      // If mapping the buffer fails, it's unlikely we can continue.
      return set_status(ReaderStatus::kIoError);
    }
    current_trace_ = trace_num;
  }
  return ReaderStatus::kOk;
}

const void* Reader::GetCurrentTraceBuffer() const {
  if (BufferMapped())
    return buffer_reader_->buffer();
  return nullptr;
}

size_t Reader::GetCurrentTraceSize() const {
  if (BufferMapped())
    return buffer_reader_->captured_bytes();
  return 0;
}

size_t Reader::GetLastRecordOffset() const {
  if (BufferMapped())
    return buffer_reader_->last_record_offset();
  return 0;
}

ReaderStatus Reader::ReadNextRecord(uint32_t* trace_num,
                                    SampleRecord* record) {
  if (status_ != ReaderStatus::kOk) {
    return status_;
  }

  while (current_trace_ < num_traces_) {
    // If this is the first trace, or if we're done with this trace's records,
    // move to the next trace.
    if (!BufferMapped() || buffer_reader_->status() != ReaderStatus::kOk) {
      uint32_t next_trace = 0;
      if (BufferMapped())
        next_trace = current_trace_ + 1;
      if (next_trace >= num_traces_)
        break;
      // Out with the old, in with the new.
      ReaderStatus status = SetTrace(next_trace);
      if (status != ReaderStatus::kOk)
        return status;
    }

    ReaderStatus status = buffer_reader_->ReadNextRecord(record);
    if (status != ReaderStatus::kOk) {
      // Even if there's an error reading this buffer's records, keep reading
      // the rest of them.
      continue;
    }

    *trace_num = current_trace_;
    return ReaderStatus::kOk;
  }

  return set_status(ReaderStatus::kNoMoreRecords);
}

}  // namespace perfmon
