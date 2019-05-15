// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/syslog/wire_format.h>
#include <zircon/syscalls/log.h>
#include <zircon/types.h>

#include <vector>

#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace {

class StubLogListener : public fuchsia::logger::LogListener {
 public:
  StubLogListener();
  ~StubLogListener() override;

  void LogMany(::std::vector<fuchsia::logger::LogMessage> Log) override;
  void Log(fuchsia::logger::LogMessage Log) override;
  void Done() override;

  const std::vector<fuchsia::logger::LogMessage>& GetLogs() {
    return log_messages_;
  }
  bool ListenFiltered(sys::ComponentContext* component_context, zx_koid_t pid,
                      const std::string& tag);

 private:
  ::fidl::Binding<fuchsia::logger::LogListener> binding_;
  fuchsia::logger::LogListenerPtr log_listener_;
  std::vector<fuchsia::logger::LogMessage> log_messages_;
};

StubLogListener::StubLogListener() : binding_(this) {
  binding_.Bind(log_listener_.NewRequest());
}

StubLogListener::~StubLogListener() {}

void StubLogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> logs) {
  std::move(logs.begin(), logs.end(), std::back_inserter(log_messages_));
}

void StubLogListener::Log(fuchsia::logger::LogMessage log) {
  log_messages_.push_back(std::move(log));
}

void StubLogListener::Done() {}

bool StubLogListener::ListenFiltered(sys::ComponentContext* component_context,
                                     zx_koid_t pid, const std::string& tag) {
  if (!log_listener_) {
    return false;
  }
  auto log_service = component_context->svc()->Connect<fuchsia::logger::Log>();
  auto options = fuchsia::logger::LogFilterOptions::New();
  options->filter_by_pid = true;
  options->pid = pid;
  options->tags = {tag};
  log_service->Listen(std::move(log_listener_), std::move(options));
  return true;
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

using LoggerIntegrationTest = gtest::RealLoopFixture;

TEST_F(LoggerIntegrationTest, ListenFiltered) {
  // Make sure there is one syslog message coming from that pid and with a tag
  // unique to this test case.
  auto pid = fsl::GetCurrentProcessKoid();
  auto tag = "logger_integration_cpp_test.ListenFiltered";
  auto message = "my message";
  ASSERT_EQ(syslog::InitLogger({tag}), ZX_OK);
  FX_LOGS(INFO) << message;

  // Start the log listener and the logger, and wait for the log message to
  // arrive.
  StubLogListener log_listener;
  ASSERT_TRUE(log_listener.ListenFiltered(sys::ComponentContext::Create().get(),
                                          pid, tag));
  auto& logs = log_listener.GetLogs();
  RunLoopUntil([&logs] { return logs.size() >= 1u; });

  ASSERT_EQ(logs.size(), 1u);
  ASSERT_EQ(logs[0].tags.size(), 1u);
  EXPECT_EQ(logs[0].tags[0], tag);
  EXPECT_EQ(logs[0].severity, FX_LOG_INFO);
  EXPECT_EQ(logs[0].pid, pid);
  EXPECT_THAT(logs[0].msg, testing::EndsWith(message));
}

}  // namespace
