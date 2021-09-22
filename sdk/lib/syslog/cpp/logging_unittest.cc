// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <lib/syslog/cpp/macros.h>

#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/syslog/cpp/log_level.h"
#include "logging_backend_shared.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/uuid/uuid.h"

#ifdef __Fuchsia__
#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/async/wait.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/global.h>
#include <lib/syslog/logger.h>
#include <lib/syslog/wire_format.h>
#include <lib/zx/socket.h>

#include <cinttypes>

#include <rapidjson/document.h>

#include "fuchsia/logger/cpp/fidl.h"
#include "lib/fdio/directory.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/zx/vmo.h"
#include "src/diagnostics/lib/cpp-log-decoder/log_decoder.h"
#include "src/lib/diagnostics/accessor2logger/log_message.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#endif

namespace syslog {
namespace {

class LoggingFixture : public ::testing::Test {
 public:
  LoggingFixture() : old_severity_(GetMinLogLevel()), old_stderr_(dup(STDERR_FILENO)) {}
  ~LoggingFixture() {
    SetLogSettings({.min_log_level = old_severity_});
#ifdef __Fuchsia__
    // Go back to using STDERR.
    fx_logger_t* logger = fx_log_get_logger();
    fx_logger_activate_fallback(logger, -1);
#else
    dup2(old_stderr_, STDERR_FILENO);
#endif
  }

 private:
  LogSeverity old_severity_;
  int old_stderr_;
};

using LoggingFixtureDeathTest = LoggingFixture;

#ifdef __Fuchsia__
class FakeLogSink : public fuchsia::logger::LogSink {
 public:
  explicit FakeLogSink(async_dispatcher_t* dispatcher, zx::channel channel)
      : dispatcher_(dispatcher) {
    fidl::InterfaceRequest<fuchsia::logger::LogSink> request(std::move(channel));
    bindings_.AddBinding(this, std::move(request), dispatcher);
  }

  /// Send this socket to be drained.
  ///
  /// See //zircon/system/ulib/syslog/include/lib/syslog/wire_format.h for what
  /// is expected to be received over the socket.
  void Connect(::zx::socket socket) override {
    // Not supported by this test.
    abort();
  }

  struct Wait : async_wait_t {
    FakeLogSink* this_ptr;
    Wait* next = this;
    Wait* prev = this;
  };

  std::string rust_decode_message_to_string(uint8_t* data, size_t len) {
    auto raw_message = fuchsia_decode_log_message_to_json(data, len);
    std::string ret = raw_message;
    fuchsia_free_decoded_log_message(raw_message);
    return ret;
  }

  void OnDataAvailable(zx_handle_t socket) {
    constexpr size_t kSize = 65536;
    std::unique_ptr<unsigned char[]> data = std::make_unique<unsigned char[]>(kSize);
    size_t actual = 0;
    zx_socket_read(socket, 0, data.get(), kSize, &actual);
    std::string msg = rust_decode_message_to_string(data.get(), actual);
    fsl::SizedVmo vmo;
    fsl::VmoFromString(msg, &vmo);
    fuchsia::diagnostics::FormattedContent content;
    fuchsia::mem::Buffer buffer;
    buffer.vmo = std::move(vmo.vmo());
    buffer.size = msg.size();
    content.set_json(std::move(buffer));
    callback_.value()(std::move(content));
  }

  static void OnDataAvailable_C(async_dispatcher_t* dispatcher, async_wait_t* raw,
                                zx_status_t status, const zx_packet_signal_t* signal) {
    switch (status) {
      case ZX_OK:
        static_cast<Wait*>(raw)->this_ptr->OnDataAvailable(raw->object);
        async_begin_wait(dispatcher, raw);
        break;
      case ZX_ERR_PEER_CLOSED:
        zx_handle_close(raw->object);
        break;
    }
  }

  /// Send this socket to be drained, using the structured logs format.
  ///
  /// See //docs/reference/diagnostics/logs/encoding.md for what is expected to
  /// be received over the socket.
  void ConnectStructured(::zx::socket socket) override {
    Wait* wait = new Wait();
    waits_.push_back(wait);
    wait->this_ptr = this;
    wait->object = socket.release();
    wait->handler = OnDataAvailable_C;
    wait->options = 0;
    wait->trigger = ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READABLE;
    async_begin_wait(dispatcher_, wait);
  }

  void Collect(std::function<void(fuchsia::diagnostics::FormattedContent content)> callback) {
    callback_ = std::move(callback);
  }

  ~FakeLogSink() {
    for (auto& wait : waits_) {
      async_cancel_wait(dispatcher_, wait);
      delete wait;
    }
  }

 private:
  std::vector<Wait*> waits_;
  fidl::BindingSet<fuchsia::logger::LogSink> bindings_;
  std::optional<std::function<void(fuchsia::diagnostics::FormattedContent content)>> callback_;
  async_dispatcher_t* dispatcher_;
};

std::string SeverityToString(const int32_t severity) {
  if (severity == syslog::LOG_TRACE) {
    return "TRACE";
  } else if (severity == syslog::LOG_DEBUG) {
    return "DEBUG";
  } else if (severity > syslog::LOG_DEBUG && severity < syslog::LOG_INFO) {
    return fxl::StringPrintf("VLOG(%d)", syslog::LOG_INFO - severity);
  } else if (severity == syslog::LOG_INFO) {
    return "INFO";
  } else if (severity == syslog::LOG_WARNING) {
    return "WARN";
  } else if (severity == syslog::LOG_ERROR) {
    return "ERROR";
  } else if (severity == syslog::LOG_FATAL) {
    return "FATAL";
  }
  return "INVALID";
}

std::string Format(const fuchsia::logger::LogMessage& message) {
  return fxl::StringPrintf("[%05d.%03d][%05" PRIu64 "][%05" PRIu64 "][%s] %s: %s\n",
                           static_cast<int>(message.time / 1000000000ULL),
                           static_cast<int>((message.time / 1000000ULL) % 1000ULL), message.pid,
                           message.tid, fxl::JoinStrings(message.tags, ", ").c_str(),
                           SeverityToString(message.severity).c_str(), message.msg.c_str());
}

static std::string RetrieveLogs(std::string guid, zx::channel remote) {
  FX_LOGS(ERROR) << guid;
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  std::stringstream s;
  bool in_log = true;
  bool exit = false;
  auto log_service = std::make_unique<FakeLogSink>(loop.dispatcher(), std::move(remote));
  log_service->Collect([&](fuchsia::diagnostics::FormattedContent content) {
    if (exit) {
      return;
    }
    auto chunk_result =
        diagnostics::accessor2logger::ConvertFormattedContentToHostLogMessages(std::move(content));
    auto messages = chunk_result.take_value();  // throws exception if conversion fails.
    for (auto& msg : messages) {
      std::string formatted = Format(msg.value());
      if (formatted.find(guid) != std::string::npos) {
        if (in_log) {
          exit = true;
          loop.Quit();
          return;
        } else {
          in_log = true;
        }
      }
      if (in_log) {
        s << formatted << std::endl;
      }
    }
  });
  loop.Run();
  return s.str();
}
#endif

TEST_F(LoggingFixture, Log) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);

  int error_line = __LINE__ + 1;
  FX_LOGS(ERROR) << "something at error";

  int info_line = __LINE__ + 1;
  FX_LOGS(INFO) << "and some other at info level";

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif

  EXPECT_THAT(log, testing::HasSubstr("ERROR: [sdk/lib/syslog/cpp/logging_unittest.cc(" +
                                      std::to_string(error_line) + ")] something at error"));

  EXPECT_THAT(log, testing::HasSubstr("INFO: [logging_unittest.cc(" + std::to_string(info_line) +
                                      ")] and some other at info level"));
}

TEST_F(LoggingFixture, LogFirstN) {
  constexpr int kLimit = 5;
  constexpr int kCycles = 20;
  constexpr const char* kLogMessage = "Hello";
  static_assert(kCycles > kLimit);

  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);

  for (int i = 0; i < kCycles; ++i) {
    FX_LOGS_FIRST_N(ERROR, kLimit) << kLogMessage;
  }

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif

  int count = 0;
  size_t pos = 0;
  while ((pos = log.find(kLogMessage, pos)) != std::string::npos) {
    ++count;
    ++pos;
  }
  EXPECT_EQ(kLimit, count);
}

TEST_F(LoggingFixture, LogT) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);

  int error_line = __LINE__ + 1;
  FX_LOGST(ERROR, "first") << "something at error";

  int info_line = __LINE__ + 1;
  FX_LOGST(INFO, "second") << "and some other at info level";

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif

  EXPECT_THAT(log, testing::HasSubstr("first] ERROR: [sdk/lib/syslog/cpp/logging_unittest.cc(" +
                                      std::to_string(error_line) + ")] something at error"));

  EXPECT_THAT(log,
              testing::HasSubstr("second] INFO: [logging_unittest.cc(" + std::to_string(info_line) +
                                 ")] and some other at info level"));
}

TEST_F(LoggingFixture, VLogT) {
  LogSettings new_settings;
  new_settings.min_log_level = (LOG_INFO - 2);  // verbosity = 2
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings, {});

  int line = __LINE__ + 1;
  FX_VLOGST(1, "first") << "First message";
  FX_VLOGST(2, "second") << "ABCD";
  FX_VLOGST(3, "third") << "EFGH";

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif
  EXPECT_THAT(log, testing::HasSubstr("[first] VLOG(1): [logging_unittest.cc(" +
                                      std::to_string(line) + ")] First message"));
  EXPECT_THAT(log, testing::HasSubstr("second"));
  EXPECT_THAT(log, testing::HasSubstr("ABCD"));

  EXPECT_THAT(log, testing::Not(testing::HasSubstr("third")));
  EXPECT_THAT(log, testing::Not(testing::HasSubstr("EFGH")));
}

TEST_F(LoggingFixture, VlogVerbosity) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);

  EXPECT_EQ(0, GetVlogVerbosity());

  new_settings.min_log_level = LOG_INFO - 1;
  SetLogSettings(new_settings);

  EXPECT_EQ(1, GetVlogVerbosity());

  new_settings.min_log_level = LOG_INFO - 15;
  SetLogSettings(new_settings);

  EXPECT_EQ(15, GetVlogVerbosity());

  new_settings.min_log_level = LOG_DEBUG;
  SetLogSettings(new_settings);

  EXPECT_EQ(0, GetVlogVerbosity());
}

TEST_F(LoggingFixture, DVLogNoMinLevel) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);

  FX_DVLOGS(1) << "hello";

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif

  EXPECT_EQ(log, "");
}

TEST_F(LoggingFixture, DVLogWithMinLevel) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  new_settings.min_log_level = (LOG_INFO - 1);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);

  FX_DVLOGS(1) << "hello";

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif

#if defined(NDEBUG)
  EXPECT_EQ(log, "");
#else
  EXPECT_THAT(log, testing::HasSubstr("hello"));
#endif
}

TEST_F(LoggingFixtureDeathTest, CheckFailed) { ASSERT_DEATH(FX_CHECK(false), ""); }

#if defined(__Fuchsia__)
TEST_F(LoggingFixture, Plog) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);

  FX_PLOGS(ERROR, ZX_OK) << "should be ok";
  FX_PLOGS(ERROR, ZX_ERR_ACCESS_DENIED) << "got access denied";

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif

  EXPECT_THAT(log, testing::HasSubstr("should be ok: 0 (ZX_OK)"));
  EXPECT_THAT(log, testing::HasSubstr("got access denied: -30 (ZX_ERR_ACCESS_DENIED)"));
}

TEST_F(LoggingFixture, PlogT) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);

  int line1 = __LINE__ + 1;
  FX_PLOGST(ERROR, "abcd", ZX_OK) << "should be ok";

  int line2 = __LINE__ + 1;
  FX_PLOGST(ERROR, "qwerty", ZX_ERR_ACCESS_DENIED) << "got access denied";

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif

  EXPECT_THAT(log, testing::HasSubstr("abcd] ERROR: [sdk/lib/syslog/cpp/logging_unittest.cc(" +
                                      std::to_string(line1) + ")] should be ok: 0 (ZX_OK)"));
  EXPECT_THAT(log, testing::HasSubstr("qwerty] ERROR: [sdk/lib/syslog/cpp/logging_unittest.cc(" +
                                      std::to_string(line2) +
                                      ")] got access denied: -30 (ZX_ERR_ACCESS_DENIED)"));
}
#endif  // defined(__Fuchsia__)

TEST_F(LoggingFixture, SLog) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);
  std::string log_id = uuid::Generate();

  int line1 = __LINE__ + 1;
  FX_SLOG(ERROR, nullptr, "some_msg", "String log");

  int line2 = __LINE__ + 1;
  FX_SLOG(ERROR, nullptr, "some_msg", 42);

  int line4 = __LINE__ + 1;
  FX_SLOG(ERROR, "msg", "first", 42, "second", "string");

  int line5 = __LINE__ + 1;
  FX_SLOG(ERROR, "String log");

  int line6 = __LINE__ + 1;
  FX_SLOG(ERROR, nullptr, "float", 0.25f);

  int line7 = __LINE__ + 1;
  FX_SLOG(ERROR, "String with quotes", "value", "char is '\"'");

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif
  EXPECT_THAT(log, testing::HasSubstr("ERROR: [" + std::string(__FILE__) + "(" +
                                      std::to_string(line1) + ")] some_msg=\"String log\""));
  EXPECT_THAT(log, testing::HasSubstr("ERROR: [" + std::string(__FILE__) + "(" +
                                      std::to_string(line2) + ")] some_msg=42"));
  EXPECT_THAT(log, testing::HasSubstr("ERROR: [" + std::string(__FILE__) + "(" +
                                      std::to_string(line4) + ")] msg first=42 second=\"string\""));
  EXPECT_THAT(log, testing::HasSubstr("ERROR: [" + std::string(__FILE__) + "(" +
                                      std::to_string(line5) + ")] String log"));
  EXPECT_THAT(log, testing::HasSubstr("ERROR: [" + std::string(__FILE__) + "(" +
                                      std::to_string(line6) + ")] float=0.250000"));

  EXPECT_THAT(log,
              testing::HasSubstr("ERROR: [" + std::string(__FILE__) + "(" + std::to_string(line7) +
                                 ")] String with quotes value=\"char is '\\\"'\""));
}

TEST_F(LoggingFixture, BackendDirect) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);
  syslog_backend::LogBuffer buffer;
  syslog_backend::BeginRecord(&buffer, syslog::LOG_ERROR, "foo.cc", 42, "Log message", "condition");
  syslog_backend::WriteKeyValue(&buffer, "tag", "fake tag");
  syslog_backend::EndRecord(&buffer);
  syslog_backend::FlushRecord(&buffer);
  syslog_backend::BeginRecord(&buffer, syslog::LOG_ERROR, "foo.cc", 42, "fake message",
                              "condition");
  syslog_backend::WriteKeyValue(&buffer, "tag", "fake tag");
  syslog_backend::WriteKeyValue(&buffer, "foo", static_cast<int64_t>(42));
  syslog_backend::EndRecord(&buffer);
  syslog_backend::FlushRecord(&buffer);
#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif

  EXPECT_THAT(log,
              testing::HasSubstr("ERROR: [foo.cc(42)] Check failed: condition. Log message\n"));
  EXPECT_THAT(log, testing::HasSubstr(
                       "ERROR: [foo.cc(42)] Check failed: condition. fake message foo=42\n"));
}

TEST_F(LoggingFixture, LogId) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);

  int line = __LINE__ + 1;
  FX_LOGS(ERROR("test")) << "Hello";

#ifdef __Fuchsia__
  std::string log = RetrieveLogs(guid, std::move(remote));
#else
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
#endif
  std::cerr << log;
  auto expected = "ERROR: [sdk/lib/syslog/cpp/logging_unittest.cc(" + std::to_string(line) +
                  ")] Hello log_id=\"test\"";
  EXPECT_THAT(log, testing::HasSubstr("ERROR: [sdk/lib/syslog/cpp/logging_unittest.cc(" +
                                      std::to_string(line) + ")] Hello log_id=\"test\""));
}

TEST(StructuredLogging, LOGS) {
  std::string str;
  // 5mb log shouldn't crash
  str.resize(1000 * 5000);
  memset(str.data(), 's', str.size() - 1);
  FX_LOGS(INFO) << str;
}

#ifndef __Fuchsia__
// Fuchsia no longer uses logging_backend_shared
// since all logs are now structured.
TEST(StructuredLogging, Remaining) {
  LogSettings new_settings;
#ifdef __Fuchsia__
  auto guid = uuid::Generate();
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  new_settings.archivist_channel_override = local.release();
#else
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
#endif
  SetLogSettings(new_settings);
  syslog_backend::LogBuffer buffer;
  syslog_backend::BeginRecord(&buffer, LOG_INFO, "test", 5, "test_msg", "");
  auto header = syslog_backend::MsgHeader::CreatePtr(&buffer);
  auto initial = header->RemainingSpace();
  header->WriteChar('t');
  ASSERT_EQ(header->RemainingSpace(), initial - 1);
  header->WriteString("est");
  ASSERT_EQ(header->RemainingSpace(), initial - 4);
}

TEST(StructuredLogging, FlushAndReset) {
  syslog_backend::LogBuffer buffer;
  syslog_backend::BeginRecord(&buffer, LOG_INFO, "test", 5, "test_msg", "");
  auto header = syslog_backend::MsgHeader::CreatePtr(&buffer);
  auto initial = header->RemainingSpace();
  header->WriteString("test");
  ASSERT_EQ(header->RemainingSpace(), initial - 4);
  header->FlushAndReset();
  ASSERT_EQ(header->RemainingSpace(),
            sizeof(syslog_backend::LogBuffer::data) - 2);  // last byte reserved for NULL terminator
}
#endif

}  // namespace
}  // namespace syslog
