// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.logger/cpp/wire.h>
#include <fuchsia/device/fs/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace fdfs = fuchsia::device::fs;
namespace fio = fuchsia::io;
namespace flogger = fuchsia_logger;

class TestExporter : public fdfs::testing::Exporter_TestBase {
 public:
  using ExportHandler = fit::function<zx_status_t(std::string service_path, std::string devfs_path,
                                                  uint32_t protocol_id)>;

  void SetExportHandler(ExportHandler export_handler) {
    export_handler_ = std::move(export_handler);
  }

 private:
  void Export(fidl::InterfaceHandle<fio::Directory> service_dir, std::string service_path,
              std::string devfs_path, uint32_t protocol_id, ExportCallback callback) override {
    zx_status_t status =
        export_handler_(std::move(service_path), std::move(devfs_path), protocol_id);
    auto result = status == ZX_OK
                      ? fdfs::Exporter_Export_Result::WithResponse(fdfs::Exporter_Export_Response())
                      : fdfs::Exporter_Export_Result::WithErr(std::move(status));
    callback(std::move(result));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Exporter::%s\n", name.data());
  }

  ExportHandler export_handler_;
};

class DevfsExporterTest : public gtest::TestLoopFixture {};

zx::status<fidl::ClientEnd<fuchsia_io::Directory>> ServeSvcDir(
    component::OutgoingDirectory& outgoing) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto status = outgoing.Serve(std::move(endpoints->server));
  if (status.is_error()) {
    return status.take_error();
  }

  auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (svc_endpoints.is_error()) {
    return svc_endpoints.take_error();
  }
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)fidl::WireCall(endpoints->client)
      ->Open(fuchsia_io::wire::OpenFlags::kRightReadable, 0, "svc",
             fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel()));
  return zx::ok(std::move(svc_endpoints->client));
}

TEST_F(DevfsExporterTest, Create) {
  // Setup namespace.
  auto svc = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_EQ(ZX_OK, svc.status_value());
  auto ns = driver::testing::CreateNamespace(std::move(svc->client));
  ASSERT_TRUE(ns.is_ok());

  // Setup exporter.
  TestExporter exporter_server;
  fidl::Binding<fdfs::Exporter> exporter_binding(&exporter_server);

  driver::testing::Directory svc_directory;
  svc_directory.SetOpenHandler([this, &exporter_binding](std::string path, auto object) {
    EXPECT_EQ(path, fidl::DiscoverableProtocolName<fuchsia_device_fs::Exporter>);
    exporter_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding(&svc_directory);

  driver::testing::Directory svc_directory2;
  svc_directory2.SetOpenHandler([this, &svc_binding](std::string path, auto object) {
    EXPECT_EQ(path, ".");
    svc_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding2(&svc_directory2);

  svc_binding2.Bind(svc->server.TakeChannel(), dispatcher());

  auto outgoing = component::OutgoingDirectory::Create(dispatcher());
  auto status =
      outgoing.AddProtocol<flogger::LogSink>([](fidl::ServerEnd<flogger::LogSink> request) {});

  ASSERT_EQ(ZX_OK, status.status_value());

  auto svc_client = ServeSvcDir(outgoing);
  ASSERT_EQ(ZX_OK, svc_client.status_value());

  auto exporter = driver::DevfsExporter::Create(
      *ns, dispatcher(), fidl::WireSharedClient(std::move(*svc_client), dispatcher()));
  ASSERT_TRUE(exporter.is_ok());

  // Check export is successful.
  std::string service_path;
  std::string devfs_path;
  uint32_t protocol_id = 0;
  exporter_server.SetExportHandler([&service_path, &devfs_path, &protocol_id](
                                       std::string service, std::string devfs, uint32_t id) {
    service_path = service;
    devfs_path = devfs;
    protocol_id = id;
    return ZX_OK;
  });

  async::Executor executor(dispatcher());

  bool finished = false;
  executor.schedule_task(exporter->Export<flogger::LogSink>("sys/log", 1)
                             .and_then([&finished]() mutable { finished = true; })
                             .or_else([](const zx_status_t& status) {
                               EXPECT_EQ(ZX_OK, status);
                               return fpromise::error(status);
                             }));
  RunLoopUntilIdle();

  EXPECT_TRUE(finished);
  EXPECT_EQ(fidl::DiscoverableProtocolName<flogger::LogSink>, service_path);
  EXPECT_EQ("sys/log", devfs_path);
  EXPECT_EQ(1u, protocol_id);
}

TEST_F(DevfsExporterTest, Create_ServiceNotFound) {
  // Setup namespace.
  auto svc = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_EQ(ZX_OK, svc.status_value());
  auto ns = driver::testing::CreateNamespace(std::move(svc->client));
  ASSERT_TRUE(ns.is_ok());

  // Setup exporter.
  TestExporter exporter_server;
  fidl::Binding<fdfs::Exporter> exporter_binding(&exporter_server);

  driver::testing::Directory svc_directory;
  svc_directory.SetOpenHandler([this, &exporter_binding](std::string path, auto object) {
    EXPECT_EQ(path, fidl::DiscoverableProtocolName<fuchsia_device_fs::Exporter>);
    exporter_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding(&svc_directory);

  driver::testing::Directory svc_directory2;
  svc_directory2.SetOpenHandler([this, &svc_binding](std::string path, auto object) {
    EXPECT_EQ(path, ".");
    svc_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding2(&svc_directory2);

  svc_binding2.Bind(svc->server.TakeChannel(), dispatcher());

  auto outgoing = component::OutgoingDirectory::Create(dispatcher());
  auto status =
      outgoing.AddProtocol<flogger::LogSink>([](fidl::ServerEnd<flogger::LogSink> request) {});
  ASSERT_EQ(ZX_OK, status.status_value());
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(ZX_OK, outgoing.Serve(std::move(endpoints->server)).status_value());

  auto exporter = driver::DevfsExporter::Create(
      *ns, dispatcher(), fidl::WireSharedClient(std::move(endpoints->client), dispatcher()));
  ASSERT_TRUE(exporter.is_ok());

  // Check export failure due to missing service.
  bool finished = false;
  async::Executor executor(dispatcher());
  executor.schedule_task(exporter->Export<flogger::LogSink>("sys/log", 1)
                             .or_else([&finished](const zx_status_t& status) {
                               EXPECT_EQ(ZX_ERR_NOT_FOUND, status);
                               finished = true;
                               return fpromise::error(status);
                             }));
  RunLoopUntilIdle();
  ASSERT_TRUE(finished);
}

TEST_F(DevfsExporterTest, Create_ServiceFailure) {
  // Setup namespace.
  auto svc = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_EQ(ZX_OK, svc.status_value());
  auto ns = driver::testing::CreateNamespace(std::move(svc->client));
  ASSERT_TRUE(ns.is_ok());

  // Setup exporter.
  TestExporter exporter_server;
  fidl::Binding<fdfs::Exporter> exporter_binding(&exporter_server);

  driver::testing::Directory svc_directory;
  svc_directory.SetOpenHandler([this, &exporter_binding](std::string path, auto object) {
    EXPECT_EQ(path, fidl::DiscoverableProtocolName<fuchsia_device_fs::Exporter>);
    exporter_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding(&svc_directory);

  driver::testing::Directory svc_directory2;
  svc_directory2.SetOpenHandler([this, &svc_binding](std::string path, auto object) {
    EXPECT_EQ(path, ".");
    svc_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding2(&svc_directory2);

  svc_binding2.Bind(svc->server.TakeChannel(), dispatcher());

  auto outgoing = component::OutgoingDirectory::Create(dispatcher());
  auto status =
      outgoing.AddProtocol<flogger::LogSink>([](fidl::ServerEnd<flogger::LogSink> request) {});
  ASSERT_EQ(ZX_OK, status.status_value());

  auto svc_client = ServeSvcDir(outgoing);
  ASSERT_EQ(ZX_OK, svc_client.status_value());

  auto exporter = driver::DevfsExporter::Create(
      *ns, dispatcher(), fidl::WireSharedClient(std::move(*svc_client), dispatcher()));
  ASSERT_TRUE(exporter.is_ok());

  // Check export failure due to service failure.
  exporter_server.SetExportHandler([](std::string service_path, std::string devfs_path,
                                      uint32_t id) { return ZX_ERR_INTERNAL; });

  bool finished = false;
  auto exported = exporter->Export<flogger::LogSink>("sys/log", 1)
                      .or_else([&finished](const zx_status_t& status) {
                        EXPECT_EQ(ZX_ERR_INTERNAL, status);
                        finished = true;
                        return fpromise::error(status);
                      });
  async::Executor executor(dispatcher());
  executor.schedule_task(std::move(exported));
  RunLoopUntilIdle();
  ASSERT_TRUE(finished);
}
