// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/wire_format.h>
#include <zircon/syscalls/log.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace {

class StubLogListener : public fuchsia::logger::LogListener {
 public:
  using DoneCallback = fit::function<void()>;
  StubLogListener();
  ~StubLogListener() override;

  void LogMany(::std::vector<fuchsia::logger::LogMessage> Log) override;
  void Log(fuchsia::logger::LogMessage Log) override;
  void Done() override;

  const std::vector<fuchsia::logger::LogMessage>& GetLogs() { return log_messages_; }

  bool ListenFiltered(const std::shared_ptr<sys::ServiceDirectory>& svc, zx_koid_t pid,
                      const std::string& tag);

  bool DumpLogs(fuchsia::logger::LogPtr log_service, DoneCallback done_callback);

  bool Listen(fuchsia::logger::LogPtr log_service);

 private:
  ::fidl::Binding<fuchsia::logger::LogListener> binding_;
  fuchsia::logger::LogListenerPtr log_listener_;
  std::vector<fuchsia::logger::LogMessage> log_messages_;
  DoneCallback done_callback_;
};

StubLogListener::StubLogListener() : binding_(this) { binding_.Bind(log_listener_.NewRequest()); }

StubLogListener::~StubLogListener() {}

void StubLogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> logs) {
  std::move(logs.begin(), logs.end(), std::back_inserter(log_messages_));
}

void StubLogListener::Log(fuchsia::logger::LogMessage log) {
  log_messages_.push_back(std::move(log));
}

void StubLogListener::Done() {
  if (done_callback_) {
    done_callback_();
  }
}

bool StubLogListener::Listen(fuchsia::logger::LogPtr log_service) {
  if (!log_listener_) {
    return false;
  }
  log_service->Listen(std::move(log_listener_), nullptr);
  return true;
}

bool StubLogListener::ListenFiltered(const std::shared_ptr<sys::ServiceDirectory>& svc,
                                     zx_koid_t pid, const std::string& tag) {
  if (!log_listener_) {
    return false;
  }
  auto log_service = svc->Connect<fuchsia::logger::Log>();
  auto options = fuchsia::logger::LogFilterOptions::New();
  options->filter_by_pid = true;
  options->pid = pid;
  options->tags = {tag};
  log_service->Listen(std::move(log_listener_), std::move(options));
  return true;
}

bool StubLogListener::DumpLogs(fuchsia::logger::LogPtr log_service, DoneCallback done_callback) {
  if (!log_listener_) {
    return false;
  }
  auto options = fuchsia::logger::LogFilterOptions::New();
  log_service->DumpLogs(std::move(log_listener_), std::move(options));
  done_callback_ = std::move(done_callback);
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

using LoggerIntegrationTest = sys::testing::TestWithEnvironment;

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
  ASSERT_TRUE(log_listener.ListenFiltered(sys::ServiceDirectory::CreateFromNamespace(), pid, tag));
  auto& logs = log_listener.GetLogs();
  RunLoopUntil([&logs] { return logs.size() >= 1u; });

  ASSERT_EQ(logs.size(), 1u);
  ASSERT_EQ(logs[0].tags.size(), 1u);
  EXPECT_EQ(logs[0].tags[0], tag);
  EXPECT_EQ(logs[0].severity, FX_LOG_INFO);
  EXPECT_EQ(logs[0].pid, pid);
  EXPECT_THAT(logs[0].msg, testing::EndsWith(message));
}

TEST_F(LoggerIntegrationTest, DumpLogs) {
  auto svcs = CreateServices();
  fuchsia::sys::LaunchInfo linfo;
  linfo.url = "fuchsia-pkg://fuchsia.com/archivist#meta/archivist.cmx";
  svcs->AddServiceWithLaunchInfo(std::move(linfo), fuchsia::logger::Log::Name_);
  auto env = CreateNewEnclosingEnvironment("dump_logs", std::move(svcs));

  auto logger_service = env->ConnectToService<fuchsia::logger::Log>();

  StubLogListener log_listener;
  bool done = false;
  ASSERT_TRUE(log_listener.DumpLogs(std::move(logger_service), [&done]() { done = true; }));

  RunLoopUntil([&done] { return done; });
  auto& logs = log_listener.GetLogs();
  ASSERT_GE(logs.size(), 1u);
  EXPECT_EQ(logs[0].tags[0], "klog");
}

TEST_F(LoggerIntegrationTest, NoKlogs) {
  auto svcs = CreateServices();
  fuchsia::sys::LaunchInfo linfo;
  linfo.url = "fuchsia-pkg://fuchsia.com/archivist#meta/archivist.cmx";
  linfo.arguments.emplace({"--disable-klog"});
  fuchsia::sys::LaunchInfo linfo_dup;
  ASSERT_EQ(ZX_OK, linfo.Clone(&linfo_dup));
  svcs->AddServiceWithLaunchInfo(std::move(linfo), fuchsia::logger::Log::Name_);
  svcs->AddServiceWithLaunchInfo(std::move(linfo_dup), fuchsia::logger::LogSink::Name_);
  auto env = CreateNewEnclosingEnvironment("no_klogs", std::move(svcs));
  WaitForEnclosingEnvToStart(env.get());

  auto logger_sink = env->ConnectToService<fuchsia::logger::LogSink>();
  zx::socket logger_sock, server_end;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &logger_sock, &server_end));
  logger_sink->Connect(std::move(server_end));

  const char* tag = "my-tag";
  const char** tags = &tag;

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = logger_sock.release(),
                               .tags = tags,
                               .num_tags = 1};

  fx_logger_t* logger;
  ASSERT_EQ(ZX_OK, fx_logger_create(&config, &logger));
  ASSERT_EQ(ZX_OK, fx_logger_log(logger, FX_LOG_INFO, nullptr, "hello world"));

  StubLogListener log_listener;
  ASSERT_TRUE(log_listener.Listen(env->ConnectToService<fuchsia::logger::Log>()));

  RunLoopUntil([&log_listener]() { return !log_listener.GetLogs().empty(); });
  auto& logs = log_listener.GetLogs();
  ASSERT_EQ(logs.size(), 1u);
  auto& msg = logs[0];
  ASSERT_EQ(msg.tags.size(), 1u);
  ASSERT_EQ(msg.tags[0], tag);
}

}  // namespace
