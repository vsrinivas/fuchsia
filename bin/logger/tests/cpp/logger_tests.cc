// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <fuchsia/logger/cpp/fidl.h>
#include <syslog/wire_format.h>
#include <zircon/syscalls/log.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/gtest/real_loop_fixture.h"
#include "lib/syslog/cpp/logger.h"

namespace {

class LogListenerMock : public fuchsia::logger::LogListener {
 public:
  LogListenerMock();

  void LogMany(::fidl::VectorPtr<fuchsia::logger::LogMessage> Log) override;
  void Log(fuchsia::logger::LogMessage Log) override;
  void Done() override;
  ~LogListenerMock() override;

  const std::vector<fuchsia::logger::LogMessage>& GetLogs() {
    return log_messages_;
  }
  void CollectLogs(size_t expected_logs);
  bool ConnectToLogger(fuchsia::sys::StartupContext* startup_context,
                       zx_koid_t pid);

 private:
  ::fidl::Binding<fuchsia::logger::LogListener> binding_;
  fuchsia::logger::LogListenerPtr log_listener_;
  std::vector<fuchsia::logger::LogMessage> log_messages_;
};

LogListenerMock::LogListenerMock() : binding_(this) {
  binding_.Bind(log_listener_.NewRequest());
}

LogListenerMock::~LogListenerMock() {}

void LogListenerMock::LogMany(
    ::fidl::VectorPtr<fuchsia::logger::LogMessage> logs) {
  std::move(logs->begin(), logs->end(), std::back_inserter(log_messages_));
}

void LogListenerMock::Log(fuchsia::logger::LogMessage log) {
  log_messages_.push_back(std::move(log));
}

void LogListenerMock::Done() {}

bool LogListenerMock::ConnectToLogger(
    fuchsia::sys::StartupContext* startup_context, zx_koid_t pid) {
  if (!log_listener_) {
    return false;
  }
  auto log_service =
      startup_context->ConnectToEnvironmentService<fuchsia::logger::Log>();
  auto options = fuchsia::logger::LogFilterOptions::New();
  options->filter_by_pid = true;
  options->pid = pid;
  // make tags non-null.
  options->tags.resize(0);
  log_service->Listen(std::move(log_listener_), std::move(options));
  return true;
}

using LoggerTest = gtest::RealLoopFixture;

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_koid_t GetCurrentProcessKoid() {
  auto koid = GetKoid(zx::process::self().get());
  ZX_DEBUG_ASSERT(koid != ZX_KOID_INVALID);
  return koid;
}

// This function will fail to build when zircon ABI changes
// and we will need to manually roll changes.
TEST(CAbi, Abi) {
  static_assert(FX_LOG_MAX_DATAGRAM_LEN == 2032, "");
  static_assert(sizeof(fx_log_metadata_t) == 32, "");
  fx_log_packet_t packet;
  static_assert(sizeof(packet.data) == 2000, "");

  // test alignment
  static_assert(offsetof(fx_log_packet_t, data) == 32, "");
  static_assert(offsetof(fx_log_packet_t, metadata) == 0, "");
  static_assert(offsetof(fx_log_metadata_t, pid) == 0, "");
  static_assert(offsetof(fx_log_metadata_t, tid) == 8, "");
  static_assert(offsetof(fx_log_metadata_t, time) == 16, "");
  static_assert(offsetof(fx_log_metadata_t, severity) == 24, "");
  static_assert(offsetof(fx_log_metadata_t, dropped_logs) == 28, "");
}

// This function will fail to build when zircon ABI changes
// and we will need to manually roll changes.
TEST(CAbi, LogRecordAbi) {
  static_assert(ZX_LOG_RECORD_MAX == 256, "");
  static_assert(ZX_LOG_FLAG_READABLE == 0x40000000, "");

  // test alignment
  static_assert(offsetof(zx_log_record_t, timestamp) == 8, "");
  static_assert(offsetof(zx_log_record_t, pid) == 16, "");
  static_assert(offsetof(zx_log_record_t, tid) == 24, "");
  static_assert(offsetof(zx_log_record_t, data) == 32, "");
}

TEST_F(LoggerTest, Integration) {
  LogListenerMock log_listener;

  auto pid = GetCurrentProcessKoid();

  auto tag = "logger_integration_cpp_test";
  ASSERT_EQ(syslog::InitLogger({tag}), ZX_OK);
  FX_LOGS(INFO) << "my message";
  auto startup_context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  ASSERT_TRUE(log_listener.ConnectToLogger(startup_context.get(), pid));
  auto& logs = log_listener.GetLogs();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&logs] { return logs.size() >= 1u; },
                                        zx::sec(5)));
  ASSERT_EQ(logs.size(), 1u);
  ASSERT_EQ(logs[0].tags->size(), 1u);
  EXPECT_EQ(logs[0].tags.get()[0].get(), tag);
  EXPECT_EQ(logs[0].severity, FX_LOG_INFO);
  EXPECT_EQ(logs[0].pid, pid);
}

}  // namespace
