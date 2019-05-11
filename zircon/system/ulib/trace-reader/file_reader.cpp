// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-reader/file_reader.h>

namespace trace {

// static
bool FileReader::Create(const char* file_path,
                        RecordConsumer record_consumer,
                        ErrorHandler error_handler,
                        std::unique_ptr<FileReader>* out_reader) {
    ZX_DEBUG_ASSERT(out_reader != nullptr);
    FILE* f = fopen(file_path, "rb");
    if (f == nullptr) {
        return false;
    }

    out_reader->reset(new FileReader(f, std::move(record_consumer),
                                     std::move(error_handler)));
    return true;
}

FileReader::FileReader(FILE* file, RecordConsumer record_consumer,
                       ErrorHandler error_handler)
    : TraceReader(std::move(record_consumer),
                  std::move(error_handler)),
      file_(file) {
}

void FileReader::ReadFile() {
    for (;;) {
        size_t to_read = buffer_.size() - buffer_end_;
        size_t actual = fread(buffer_.data() + buffer_end_, 1u, to_read, file_);

        if (actual == 0) {
            break;
        }

        buffer_end_ += actual;
        size_t bytes_available = buffer_end_;

        size_t bytes_consumed;
        trace::Chunk chunk(reinterpret_cast<const uint64_t*>(buffer_.data()),
                           trace::BytesToWords(bytes_available));
        if (!ReadRecords(chunk)) {
            ReportError("Trace stream is corrupted");
            break;
        }
        bytes_consumed =
            bytes_available - trace::WordsToBytes(chunk.remaining_words());

        bytes_available -= bytes_consumed;
        memmove(buffer_.data(), buffer_.data() + bytes_consumed, bytes_available);
        buffer_end_ = bytes_available;
    }
}

} // namespace trace
