// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRACE_READER_FILE_READER_H_
#define TRACE_READER_FILE_READER_H_

#include <stdio.h>

#include <array>
#include <memory>

#include <trace-engine/fields.h>
#include <trace-reader/reader.h>

namespace trace {

// Read records from a file, in fxt file format.

class FileReader : public TraceReader {
 public:
  static bool Create(const char* file_path, RecordConsumer record_consumer,
                     ErrorHandler error_handler, std::unique_ptr<FileReader>* out_reader);

  void ReadFile();

 private:
  // Note: Buffer needs to be big enough to store records of maximum size.
  static constexpr size_t kReadBufferSize = trace::RecordFields::kMaxRecordSizeBytes * 4;

  explicit FileReader(FILE* file, RecordConsumer record_consumer, ErrorHandler error_handler);

  FILE* const file_;
  RecordConsumer const record_consumer_;
  ErrorHandler const error_handler_;

  // We don't use a vector here to avoid the housekeeping necessary to keep
  // checkers happy (e.g., asan). We use this buffer in an atypical way.
  std::array<uint8_t, kReadBufferSize> buffer_;
  // The amount of space in use in |buffer_|.
  size_t buffer_end_ = 0u;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(FileReader);
};

}  // namespace trace

#endif  // TRACE_READER_FILE_READER_H_
