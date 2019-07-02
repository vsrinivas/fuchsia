// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_LOG_SINK_H_
#define ZXTEST_BASE_LOG_SINK_H_

#include <cstdio>

#include <lib/fit/function.h>
#include <zircon/compiler.h>

namespace zxtest {

// This class provides an abstraction for writing log messages. This allow
// redirecting the output of the reporter on runtime, or even supressing it.
class LogSink {
 public:
  LogSink(const LogSink&) = delete;
  LogSink(LogSink&&) = delete;
  LogSink& operator=(const LogSink&) = delete;
  LogSink& operator=(LogSink&&) = delete;
  virtual ~LogSink() = default;

  // Writes the formatted string to some place defined by the specific implementation.
  virtual void Write(const char* format, ...) __PRINTFLIKE(2, 3) = 0;

  // Flushes the contents to a persistent or final location. Some implementations may choose to
  // optionally flush the contents under certain conditions.
  virtual void Flush() = 0;

 protected:
  LogSink() = default;
};

// Represents a LogSink that writes to a |FILE*| stream.
class FileLogSink : public LogSink {
 public:
  // Constructs a LogSink that writes to |stream|. If |stream| is nullptr then the outputs are
  // silenced.
  // This constructor assumes that |stream| is owned by the caller.
  explicit FileLogSink(FILE* stream);

  // Constructs a LogSink that writes to |stream|. If |stream| is nullptr then the outputs are
  // silenced.
  // This constructor takes ownership of |stream|, and will call |close_fn(stream)| on destruction.
  FileLogSink(FILE* stream, fit::function<void(FILE*)> close_fn);
  FileLogSink(const FileLogSink&) = delete;
  FileLogSink(FileLogSink&&) = delete;
  FileLogSink& operator=(const FileLogSink&) = delete;
  FileLogSink& operator=(FileLogSink&&) = delete;
  ~FileLogSink() final;

  // Writes |format| to the underlying |stream_|.
  void Write(const char* format, ...) final __PRINTFLIKE(2, 3);

  // Flushes the contents of the buffered for |stream_|.
  void Flush() final;

 protected:
  FILE* stream_ = nullptr;
  fit::function<void(FILE*)> stream_closer_ = nullptr;
};

}  // namespace zxtest

#endif  // ZXTEST_BASE_LOG_SINK_H_
