// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/logger.h"

#include <fidl/fuchsia.logger/cpp/wire.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/wire_format.h>

#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include "src/devices/lib/driver2/test_base.h"
#include "src/diagnostics/lib/cpp-log-decoder/log_decoder.h"
#include "src/lib/diagnostics/accessor2logger/log_message.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"

namespace fio = fuchsia::io;
namespace flogger = fuchsia::logger;
namespace frunner = fuchsia_component_runner;

constexpr char kName[] = "my-name";
constexpr char kMessage[] = "my-message";

class TestLogSink : public flogger::testing::LogSink_TestBase {
 public:
  using ConnectHandler = fit::function<void(zx::socket socket)>;

  void SetConnectHandler(ConnectHandler connect_handler) {
    connect_handler_ = std::move(connect_handler);
  }

 private:
  void ConnectStructured(::zx::socket socket) override { connect_handler_(std::move(socket)); }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: LogSink::%s\n", name.data());
  }

  ConnectHandler connect_handler_;
};

void CheckLogUnreadable(zx::socket& log_socket) {
  zx_signals_t pending = ZX_SIGNAL_NONE;
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            log_socket.wait_one(ZX_SOCKET_READABLE, zx::time::infinite_past(), &pending));
  EXPECT_EQ(ZX_SOCKET_WRITABLE, pending);
}

std::string rust_decode_message_to_string(uint8_t* data, size_t len) {
  auto raw_message = fuchsia_decode_log_message_to_json(data, len);
  std::string ret = raw_message;
  fuchsia_free_decoded_log_message(raw_message);
  return ret;
}

struct DecodedLogMessage {
  fuchsia::logger::LogMessage message;
  rapidjson::Document document;
};

DecodedLogMessage decode_log_message_to_struct(uint8_t* data, size_t len) {
  fsl::SizedVmo vmo;
  auto msg = rust_decode_message_to_string(data, len);
  fsl::VmoFromString(msg, &vmo);
  fuchsia::diagnostics::FormattedContent content;
  fuchsia::mem::Buffer buffer;
  buffer.vmo = std::move(vmo.vmo());
  buffer.size = msg.size();
  content.set_json(std::move(buffer));
  DecodedLogMessage ret;
  ret.message =
      diagnostics::accessor2logger::ConvertFormattedContentToHostLogMessages(std::move(content))
          .take_value()[0]
          .take_value();
  ret.document.Parse(msg);
  return ret;
}

void CheckLogReadable(zx::socket& log_socket, fx_log_severity_t severity) {
  // Check state of logger after writing info log.
  zx_signals_t pending = ZX_SIGNAL_NONE;
  EXPECT_EQ(ZX_OK, log_socket.wait_one(ZX_SOCKET_READABLE, zx::time::infinite_past(), &pending));
  EXPECT_EQ(ZX_SOCKET_READABLE | ZX_SOCKET_WRITABLE, pending);

  // Read from the log socket.
  uint8_t packet[ZX_CHANNEL_MAX_MSG_BYTES];
  size_t actual = 0;
  ASSERT_EQ(ZX_OK, log_socket.read(0, &packet, sizeof(packet), &actual));
  EXPECT_LT(actual, sizeof(packet));
  auto msg = decode_log_message_to_struct(packet, actual);
  EXPECT_EQ(severity, msg.message.severity);
  EXPECT_EQ(msg.message.tags[0], kName);
  EXPECT_EQ(std::string(msg.document[0]["payload"]["root"]["message"]["value"].GetString()),
            kMessage);
}

TEST(LoggerTest, CreateAndLog) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Setup namespace.
  auto svc = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_EQ(ZX_OK, svc.status_value());
  auto ns = driver::testing::CreateNamespace(std::move(svc->client));
  ASSERT_TRUE(ns.is_ok());

  // Setup logger.
  zx::socket log_socket;
  TestLogSink log_sink;
  log_sink.SetConnectHandler([&log_socket](zx::socket socket) { log_socket = std::move(socket); });
  fidl::Binding<flogger::LogSink> log_binding(&log_sink);

  driver::testing::Directory svc_directory;
  svc_directory.SetOpenHandler([&loop, &log_binding](std::string path, auto object) {
    EXPECT_EQ(fidl::DiscoverableProtocolName<fuchsia_logger::LogSink>, path);
    log_binding.Bind(object.TakeChannel(), loop.dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding(&svc_directory);
  svc_binding.Bind(svc->server.TakeChannel(), loop.dispatcher());

  auto logger = driver::Logger::Create(*ns, loop.dispatcher(), kName);
  ASSERT_TRUE(logger.is_ok());
  loop.RunUntilIdle();

  // Check initial state of logger.
  ASSERT_TRUE(log_socket.is_valid());
  CheckLogUnreadable(log_socket);

  // Check state of logger after writing logs that were below |min_severity|.
  FDF_LOGL(TRACE, logger.value(), kMessage);
  CheckLogUnreadable(log_socket);
  FDF_LOGL(DEBUG, logger.value(), kMessage);
  CheckLogUnreadable(log_socket);

  // Check state of logger after writing logs.
  FDF_LOGL(INFO, logger.value(), kMessage);
  CheckLogReadable(log_socket, FX_LOG_INFO);
  FDF_LOGL(WARNING, logger.value(), kMessage);
  CheckLogReadable(log_socket, FX_LOG_WARNING);
  FDF_LOGL(ERROR, logger.value(), kMessage);
  CheckLogReadable(log_socket, FX_LOG_ERROR);
}

TEST(LoggerTest, Create_NoLogSink) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Setup namespace.
  auto pkg = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_EQ(ZX_OK, pkg.status_value());
  fidl::Arena arena;
  fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(arena, 1);
  ns_entries[0].Allocate(arena);
  ns_entries[0].set_path(arena, "/pkg").set_directory(std::move(pkg->client));
  auto ns = driver::Namespace::Create(ns_entries);
  ASSERT_TRUE(ns.is_ok());

  // Setup logger.
  auto logger = driver::Logger::Create(*ns, loop.dispatcher(), kName);
  ASSERT_TRUE(logger.is_error());
}
