// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdarg>
#include <cstdio>
#include <utility>

#include <zxtest/base/log-sink.h>

namespace zxtest {

FileLogSink::FileLogSink(FILE* stream) : LogSink(), stream_(stream), stream_closer_(nullptr) {
}

FileLogSink::FileLogSink(FILE* stream, fit::function<void(FILE*)> stream_closer)
    : LogSink(), stream_(stream), stream_closer_(std::move(stream_closer)) {
}

FileLogSink::~FileLogSink() {
  if (stream_closer_ != nullptr && stream_ != nullptr) {
    stream_closer_(stream_);
  }
}

void FileLogSink::Write(const char* format, ...) {
  if (stream_ == nullptr) {
    return;
  }
  va_list args;
  va_start(args, format);
  vfprintf(stream_, format, args);
  va_end(args);
}

void FileLogSink::Flush() {
  if (stream_ == nullptr) {
    return;
  }
  fflush(stream_);
}

}  // namespace zxtest
