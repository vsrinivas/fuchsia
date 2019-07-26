// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include <fbl/function.h>
#include <zxtest/base/log-sink.h>
#include <zxtest/base/reporter.h>
#include <zxtest/base/runner.h>

namespace zxtest {
namespace test {
namespace {

// Returns a new memfile that will be created at |path|.
FILE* MakeMemfile(char* buffer, const char* path, const uint64_t size) {
  memset(buffer, '\0', size);
  FILE* memfile = fmemopen(buffer, size, "a");
  return memfile;
}

// Fake class that simply checks whether something was written or not
// to the sink.
class FakeLogSink : public LogSink {
 public:
  FakeLogSink() = default;
  FakeLogSink(FakeLogSink&&) = delete;
  FakeLogSink& operator=(const FakeLogSink&) = delete;
  FakeLogSink& operator=(FakeLogSink&&) = delete;
  ~FakeLogSink() final = default;

  // Writes |format| to the underlying |stream_|.
  void Write(const char* format, ...) final __PRINTFLIKE(2, 3) { is_written_ = true; }

  // Flushes the contents of the buffered for |stream_|.
  void Flush() final {}

  void Reset() { is_written_ = false; }

  bool IsWritten() const { return is_written_; }

 private:
  bool is_written_ = false;
};

}  // namespace

void ReporterWritesToLogSink() {
  std::unique_ptr<FakeLogSink> log_sink(new FakeLogSink());
  FakeLogSink* log_sink_ptr = log_sink.get();
  Reporter reporter(std::move(log_sink));

  ZX_ASSERT_MSG(log_sink_ptr == reporter.mutable_log_sink(), "LogSink not set correctly");

  // Passing the global singleton of reporter, since is const reference.
  reporter.OnProgramStart(*zxtest::Runner::GetInstance());

  ZX_ASSERT_MSG(log_sink_ptr->IsWritten(), "Failed to write to LogSink\n");
}

void ReporterSetLogSink() {
  std::unique_ptr<FakeLogSink> log_sink(new FakeLogSink());
  std::unique_ptr<FakeLogSink> log_sink_2(new FakeLogSink());
  FakeLogSink* log_sink_ptr = log_sink.get();
  FakeLogSink* log_sink_ptr_2 = log_sink_2.get();
  Reporter reporter(std::move(log_sink));

  ZX_ASSERT_MSG(log_sink_ptr == reporter.mutable_log_sink(), "LogSink not set correctly");

  // Passing the global singleton of reporter, since is const reference.
  reporter.OnProgramStart(*zxtest::Runner::GetInstance());

  ZX_ASSERT_MSG(log_sink_ptr->IsWritten(), "Failed to write to LogSink\n");
  log_sink_ptr->Reset();
  ZX_ASSERT_MSG(!log_sink_ptr->IsWritten(), "Failed reset to LogSink\n");
  reporter.set_log_sink(std::move(log_sink_2));

  // Passing the global singleton of reporter, since is const reference.
  reporter.OnProgramStart(*zxtest::Runner::GetInstance());
  ZX_ASSERT_MSG(log_sink_ptr_2->IsWritten(), "Reporter did not wrote to the new LogSink\n");
}

void FileLogSinkCallCloserOnDestruction() {
  bool called = false;
  {
    char buffer[1024];
    FILE* memfile = MakeMemfile(buffer, "/somepath.out", 1024);
    std::unique_ptr<FileLogSink> log_sink =
        std::make_unique<FileLogSink>(memfile, [&called](FILE* memfile) {
          called = true;
          fclose(memfile);
        });
  }
  ZX_ASSERT_MSG(called == true, "FileLogSink did not call closer on destruction.\n");
}

void FileLogSinkWrite() {
  constexpr char kExpectedOutput[] = "some_content string 1\n";
  char buffer[1024];
  FILE* memfile = MakeMemfile(buffer, "/somepath.out", 1024);
  std::unique_ptr<FileLogSink> log_sink =
      std::make_unique<FileLogSink>(memfile, [](FILE* memfile) { fclose(memfile); });

  log_sink->Write("some_content %s %d\n", "string", 1);
  log_sink->Flush();
  ZX_ASSERT_MSG(strcmp(kExpectedOutput, buffer) == 0, "Failed to write formatted output\n");
}

}  // namespace test
}  // namespace zxtest
