// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/devfs_exporter.h"

#include <fidl/fuchsia.logger/cpp/wire.h>
#include <fuchsia/device/fs/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/svc/outgoing.h>

#include "src/devices/lib/driver2/test_base.h"

namespace fdfs = fuchsia::device::fs;
namespace fio = fuchsia::io;
namespace flogger = fuchsia_logger;

class TestExporter : public fdfs::testing::Exporter_TestBase {
 public:
  using ExportHandler = fit::function<zx_status_t(std::string devfs_path, uint32_t protocol_id)>;

  void SetExportHandler(ExportHandler export_handler) {
    export_handler_ = std::move(export_handler);
  }

 private:
  void Export(fidl::InterfaceHandle<fio::Node> service_node, std::string devfs_path,
              uint32_t protocol_id, ExportCallback callback) override {
    zx_status_t status = export_handler_(std::move(devfs_path), protocol_id);
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
    EXPECT_EQ(fidl::DiscoverableProtocolName<fuchsia_device_fs::Exporter>, path);
    exporter_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding(&svc_directory);
  svc_binding.Bind(svc->server.TakeChannel(), dispatcher());

  svc::Outgoing outgoing(dispatcher());
  const auto service = [](fidl::ServerEnd<flogger::LogSink> request) { return ZX_OK; };
  zx_status_t status = outgoing.svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<flogger::LogSink>, fbl::MakeRefCounted<fs::Service>(service));
  ASSERT_EQ(ZX_OK, status);

  auto exporter = driver::DevfsExporter::Create(*ns, dispatcher(), outgoing.svc_dir());
  ASSERT_TRUE(exporter.is_ok());

  // Check export is successful.
  std::string devfs_path;
  uint32_t protocol_id;
  exporter_server.SetExportHandler([&devfs_path, &protocol_id](std::string path, uint32_t id) {
    devfs_path = path;
    protocol_id = id;
    return ZX_OK;
  });

  auto exported = exporter->Export<flogger::LogSink>("sys/log", 1);
  RunLoopUntilIdle();
  driver::testing::FakeContext context;
  auto result = exported(context);
  ASSERT_TRUE(result.is_ok());
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
    EXPECT_EQ(fidl::DiscoverableProtocolName<fuchsia_device_fs::Exporter>, path);
    exporter_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding(&svc_directory);
  svc_binding.Bind(svc->server.TakeChannel(), dispatcher());

  svc::Outgoing outgoing(dispatcher());
  auto exporter = driver::DevfsExporter::Create(*ns, dispatcher(), outgoing.svc_dir());
  ASSERT_TRUE(exporter.is_ok());

  // Check export failure due to missing service.
  auto exported = exporter->Export<flogger::LogSink>("sys/log", 1);
  RunLoopUntilIdle();
  driver::testing::FakeContext context;
  auto result = exported(context);
  ASSERT_TRUE(result.is_error());
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
    EXPECT_EQ(fidl::DiscoverableProtocolName<fuchsia_device_fs::Exporter>, path);
    exporter_binding.Bind(object.TakeChannel(), dispatcher());
  });
  fidl::Binding<fio::Directory> svc_binding(&svc_directory);
  svc_binding.Bind(svc->server.TakeChannel(), dispatcher());

  svc::Outgoing outgoing(dispatcher());
  const auto service = [](fidl::ServerEnd<flogger::LogSink> request) { return ZX_OK; };
  zx_status_t status = outgoing.svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<flogger::LogSink>, fbl::MakeRefCounted<fs::Service>(service));
  ASSERT_EQ(ZX_OK, status);

  auto exporter = driver::DevfsExporter::Create(*ns, dispatcher(), outgoing.svc_dir());
  ASSERT_TRUE(exporter.is_ok());

  // Check export failure due to service failure.
  exporter_server.SetExportHandler([](std::string path, uint32_t id) { return ZX_ERR_INTERNAL; });

  auto exported = exporter->Export<flogger::LogSink>("sys/log", 1);
  RunLoopUntilIdle();
  driver::testing::FakeContext context;
  auto result = exported(context);
  ASSERT_TRUE(result.is_error());
}
