// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/channel.h>

#include <fbl/unique_fd.h>

#include "src/devices/block/bin/ftl_proxy/ftl_util.h"
#include "src/devices/block/bin/ftl_proxy/local_storage_metrics.cb.h"
#include "src/lib/fsl/syslogger/init.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);
  fsl::InitLoggerFromCommandLine(command_line, {"ftl", "ftl_proxy"});

  std::string topo_path = ftl_proxy::GetFtlTopologicalPath("/dev/class/block");
  if (topo_path.empty()) {
    FXL_LOG(ERROR) << "Unable to find FTL in device path." << std::endl;
    return -1;
  }
  auto inspect_vmo = ftl_proxy::GetFtlInspectVmo(topo_path);
  if (!inspect_vmo.is_valid()) {
    FXL_LOG(ERROR) << "No vmo found in FTL or FTL was not reached." << std::endl;
    return -1;
  }
  auto wear_count_optional = ftl_proxy::GetDeviceWearCount(inspect_vmo);
  if (!wear_count_optional.has_value()) {
    FXL_LOG(ERROR) << "No wear count provided in inspect vmo." << std::endl;
    return -1;
  }

  std::string service_path("/svc/");
  service_path.append(llcpp::fuchsia::cobalt::LoggerFactory::Name);

  zx::channel factory_client, factory_server;
  zx_status_t status = zx::channel::create(0, &factory_client, &factory_server);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create factory channel. Status: " << status << std::endl;
  }

  if ((status = fdio_service_connect(service_path.c_str(), factory_server.release())) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to obtain handle for Cobalt Service. Status: " << status << std::endl;
    return -1;
  }

  zx::channel logger, logger_server;
  if (zx::channel::create(0, &logger, &logger_server) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed create logger channel endpoints for Cobalt Service." << std::endl;
    return -1;
  }

  auto result = llcpp::fuchsia::cobalt::LoggerFactory::Call::CreateLoggerFromProjectId(
      factory_client.borrow(), cobalt_registry::kProjectId, std::move(logger_server));
  if (!result.ok()) {
    FXL_LOG(ERROR) << "Failed to Log Events. Call status " << result.status();
    if (result.error() != nullptr) {
      FXL_LOG(ERROR) << " Error: " << result.error();
    }
    FXL_LOG(ERROR) << std::endl;
    return -1;
  }

  if (result->status != llcpp::fuchsia::cobalt::Status::OK) {
    FXL_LOG(ERROR) << "Failed to create Logger. Cobalt Return Status: "
                   << static_cast<int64_t>(result->status) << std::endl;
    return 0;
  }

  uint32_t event_code = cobalt_registry::WearCountMetricDimensionMeasuredAt_Initialization;
  fidl::VectorView<uint32_t> event_codes(fidl::unowned_ptr(&event_code), 1);
  llcpp::fuchsia::cobalt::CobaltEvent event = {};
  llcpp::fuchsia::cobalt::EventPayload payload = {};
  llcpp::fuchsia::cobalt::CountEvent count;
  count.count = wear_count_optional.value();
  payload.set_event_count(fidl::unowned_ptr(&count));

  event.metric_id = cobalt_registry::kWearCountMetricId;
  event.event_codes = std::move(event_codes);
  event.payload = std::move(payload);

  // Finally use the logger to send the wear count.
  auto logger_result =
      llcpp::fuchsia::cobalt::Logger::Call::LogCobaltEvent(logger.borrow(), std::move(event));
  if (!logger_result.ok()) {
    FXL_LOG(ERROR) << "Failed to Log Events. Call status " << logger_result.status();
    if (logger_result.error() != nullptr) {
      FXL_LOG(ERROR) << " Error: " << logger_result.error();
    }
    FXL_LOG(ERROR) << std::endl;
    return 0;
  }

  if (logger_result->status != llcpp::fuchsia::cobalt::Status::OK) {
    FXL_LOG(ERROR) << "Failed to Log Events. Cobalt Return Status: "
                   << static_cast<int64_t>(logger_result->status) << std::endl;
    return 0;
  }
  FXL_LOG(INFO) << "FTL Wear Count of " << wear_count_optional.value()
                << " successfully logged to cobalt." << std::endl;
  return 0;
}
