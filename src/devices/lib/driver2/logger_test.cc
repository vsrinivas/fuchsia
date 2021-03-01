// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/logger.h"

#include <fuchsia/io/cpp/fidl_test_base.h>
#include <fuchsia/logger/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/wire_format.h>

#include <gtest/gtest.h>

#include "src/devices/lib/driver2/test_base.h"

namespace fio = fuchsia::io;
namespace flogger = fuchsia::logger;
namespace frunner = llcpp::fuchsia::component::runner;

constexpr char kName[] = "my-name";
constexpr char kMessage[] = "my-message";

class TestLogSink : public flogger::testing::LogSink_TestBase {
 public:
  using ConnectHandler = fit::function<void(zx::socket socket)>;

  void SetConnectHandler(ConnectHandler connect_handler) {
    connect_handler_ = std::move(connect_handler);
  }

 private:
  void Connect(::zx::socket socket) override { connect_handler_(std::move(socket)); }

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

void CheckLogReadable(zx::socket& log_socket, fx_log_severity_t severity) {
  // Check state of logger after writing info log.
  zx_signals_t pending = ZX_SIGNAL_NONE;
  EXPECT_EQ(ZX_OK, log_socket.wait_one(ZX_SOCKET_READABLE, zx::time::infinite_past(), &pending));
  EXPECT_EQ(ZX_SOCKET_READABLE | ZX_SOCKET_WRITABLE, pending);

  // Read from the log socket.
  fx_log_packet_t packet = {};
  size_t actual = 0;
  ASSERT_EQ(ZX_OK, log_socket.read(0, &packet, sizeof(packet), &actual));
  EXPECT_LT(sizeof(fx_log_metadata_t), actual);
  EXPECT_EQ(severity, packet.metadata.severity);
  EXPECT_NE(nullptr, memmem(packet.data, sizeof(packet.data), kName, sizeof(kName)));
  EXPECT_NE(nullptr, memmem(packet.data, sizeof(packet.data), kMessage, sizeof(kMessage)));
}

TEST(LoggerTest, CreateAndLog) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Setup namespace.
  auto svc = fidl::CreateEndpoints<llcpp::fuchsia::io::Directory>();
  EXPECT_EQ(ZX_OK, svc.status_value());
  frunner::wire::ComponentNamespaceEntry ns_entries[] = {
      frunner::wire::ComponentNamespaceEntry::Builder(
          std::make_unique<frunner::wire::ComponentNamespaceEntry::Frame>())
          .set_path(std::make_unique<fidl::StringView>("/svc"))
          .set_directory(std::make_unique<fidl::ClientEnd<llcpp::fuchsia::io::Directory>>(
              std::move(svc->client)))
          .build(),
  };
  auto ns_vec = fidl::unowned_vec(ns_entries);
  auto ns = Namespace::Create(ns_vec);
  ASSERT_TRUE(ns.is_ok());

  // Setup logger.
  zx::socket log_socket;
  TestLogSink log_sink;
  log_sink.SetConnectHandler([&log_socket](zx::socket socket) { log_socket = std::move(socket); });
  fidl::Binding<flogger::LogSink> log_binding(&log_sink);

  TestDirectory svc_directory;
  svc_directory.SetOpenHandler([&loop, &log_binding](std::string path, auto object) {
    EXPECT_EQ("fuchsia.logger.LogSink", path);
    log_binding.Bind(object.TakeChannel(), loop.dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding(&svc_directory);
  svc_binding.Bind(svc->server.TakeChannel(), loop.dispatcher());

  auto logger = Logger::Create(ns.value(), loop.dispatcher(), kName);
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
  auto pkg = fidl::CreateEndpoints<llcpp::fuchsia::io::Directory>();
  EXPECT_EQ(ZX_OK, pkg.status_value());
  frunner::wire::ComponentNamespaceEntry ns_entries[] = {
      frunner::wire::ComponentNamespaceEntry::Builder(
          std::make_unique<frunner::wire::ComponentNamespaceEntry::Frame>())
          .set_path(std::make_unique<fidl::StringView>("/pkg"))
          .set_directory(std::make_unique<fidl::ClientEnd<llcpp::fuchsia::io::Directory>>(
              std::move(pkg->client)))
          .build(),
  };
  auto ns_vec = fidl::unowned_vec(ns_entries);
  auto ns = Namespace::Create(ns_vec);
  ASSERT_TRUE(ns.is_ok());

  // Setup logger.
  auto logger = Logger::Create(ns.value(), loop.dispatcher(), kName);
  ASSERT_TRUE(logger.is_error());
}
