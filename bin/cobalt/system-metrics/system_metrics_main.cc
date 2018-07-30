// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.

#include <fcntl.h>
#include <chrono>
#include <memory>
#include <thread>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/resource.h>
#include <zircon/device/device.h>
#include <zircon/device/sysinfo.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/logging.h"

constexpr char kConfigBinProtoPath[] = "/pkg/data/cobalt_config.binproto";
const uint32_t kUptimeMetricId = 1;
const uint32_t kMemoryUsageMetricId = 2;
const uint32_t kRawEncodingId = 1;
const unsigned int kIntervalMinutes = 1;

// Gets the root resource which is needed in order to access a variety of system
// metrics, including memory usage data.
zx_status_t get_root_resource(zx::resource* resource_out) {
  static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open " << kResourcePath << " with "
                   << strerror(errno);
    return ZX_ERR_IO;
  }

  zx_handle_t raw_resource;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &raw_resource);
  if (n != sizeof(zx_handle_t)) {
    FXL_LOG(ERROR) << "Failed to get root resource: " << n;
    return ZX_ERR_IO;
  }
  close(fd);
  resource_out->reset(raw_resource);
  return ZX_OK;
}

std::string StatusToString(fuchsia::cobalt::Status status) {
  switch (status) {
    case fuchsia::cobalt::Status::OK:
      return "OK";
    case fuchsia::cobalt::Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case fuchsia::cobalt::Status::OBSERVATION_TOO_BIG:
      return "OBSERVATION_TOO_BIG";
    case fuchsia::cobalt::Status::TEMPORARILY_FULL:
      return "TEMPORARILY_FULL";
    case fuchsia::cobalt::Status::SEND_FAILED:
      return "SEND_FAILED";
    case fuchsia::cobalt::Status::FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case fuchsia::cobalt::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
};

class SystemMetricsApp {
 public:
  // tick_interval_minutes is the number of minutes to sleep in between calls to
  // the GatherMetrics method.
  SystemMetricsApp(unsigned int tick_interval_minutes)
      : context_(component::StartupContext::CreateFromStartupInfo()),
        start_time_(std::chrono::steady_clock::now()),
        tick_interval_(tick_interval_minutes) {}

  // Main is invoked to initialize the app and start the metric gathering loop.
  void Main();

 private:
  void ConnectToEnvironmentService();

  void GatherMetrics();

  // LogUptime returns the status returned by its call to Add*Observation.
  fuchsia::cobalt::Status LogUptime(std::chrono::minutes uptime_minutes);

  // LogMemoryUsage returns the status OK if everything went fine, or the
  // logging was skipped due to scheduling, INTERNAL_ERROR if it was somehow
  // unable to get the memory usage information and whatever was returned by
  // Add*Observation otherwise.
  fuchsia::cobalt::Status LogMemoryUsage(std::chrono::minutes uptime_minutes);

 private:
  std::unique_ptr<component::StartupContext> context_;
  fuchsia::cobalt::EncoderSyncPtr encoder_;
  std::chrono::steady_clock::time_point start_time_;
  std::chrono::minutes tick_interval_;
  // We don't log every minute of uptime. We log in exponentially-growing
  // increments. This keeps track of which minute should be logged.
  int next_uptime_bucket_ = 0;

  // We log memory usage no more than once every 5 minutes.
  int next_log_memory_usage_ = 0;
};

void SystemMetricsApp::GatherMetrics() {
  auto now = std::chrono::steady_clock::now();
  auto uptime = now - start_time_;
  auto uptime_minutes =
      std::chrono::duration_cast<std::chrono::minutes>(uptime);

  LogUptime(uptime_minutes);
  LogMemoryUsage(uptime_minutes);
}

fuchsia::cobalt::Status SystemMetricsApp::LogUptime(
    std::chrono::minutes uptime_minutes) {
  while (next_uptime_bucket_ <= uptime_minutes.count()) {
    fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;

    encoder_->AddIntObservation(kUptimeMetricId, kRawEncodingId,
                                next_uptime_bucket_, &status);
    // If we failed to send an observation, we stop gathering metrics for up to
    // one minute.
    if (status != fuchsia::cobalt::Status::OK) {
      FXL_LOG(ERROR) << "AddIntObservation() => " << StatusToString(status);
      return status;
    }

    if (next_uptime_bucket_ == 0) {
      next_uptime_bucket_ = 1;
    } else {
      next_uptime_bucket_ *= 2;
    }
  }

  return fuchsia::cobalt::Status::OK;
}

fuchsia::cobalt::Status SystemMetricsApp::LogMemoryUsage(
    std::chrono::minutes uptime_minutes) {
  if (uptime_minutes.count() < next_log_memory_usage_) {
    return fuchsia::cobalt::Status::OK;
  }

  zx::resource root_resource;
  zx_status_t status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "get_root_resource failed!!!";
    return fuchsia::cobalt::Status::INTERNAL_ERROR;
  }

  zx_info_kmem_stats_t stats;
  status = zx_object_get_info(root_resource.get(), ZX_INFO_KMEM_STATS, &stats,
                              sizeof(stats), NULL, NULL);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_info failed with " << status << ".";
    return fuchsia::cobalt::Status::INTERNAL_ERROR;
  }

  auto values = fidl::VectorPtr<fuchsia::cobalt::ObservationValue>::New(3);
  // Metric part name as defined in the corresponding metric config.
  values->at(0).name = "system_uptime_minutes";
  values->at(0).value.set_int_value(uptime_minutes.count());
  values->at(0).encoding_id = kRawEncodingId;

  // Metric part name as defined in the corresponding metric config.
  values->at(1).name = "total_system_memory";
  values->at(1).value.set_int_value(stats.total_bytes);
  values->at(1).encoding_id = kRawEncodingId;

  // Metric part name as defined in the corresponding metric config.
  values->at(2).name = "free_memory";
  values->at(2).value.set_int_value(stats.free_bytes);
  values->at(2).encoding_id = kRawEncodingId;

  auto cobalt_status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  encoder_->AddMultipartObservation(kMemoryUsageMetricId, std::move(values),
                                    &cobalt_status);
  if (cobalt_status != fuchsia::cobalt::Status::OK) {
    FXL_LOG(ERROR) << "AddMultipartObservation() => "
                   << StatusToString(cobalt_status);
    return cobalt_status;
  }

  // The next time to log is in 5 minutes.
  next_log_memory_usage_ = uptime_minutes.count() + 5;
  return fuchsia::cobalt::Status::OK;
}

void SystemMetricsApp::Main() {
  ConnectToEnvironmentService();
  // We keep gathering metrics until this process is terminated.
  for (;;) {
    GatherMetrics();
    std::this_thread::sleep_for(tick_interval_);
  }
}

void SystemMetricsApp::ConnectToEnvironmentService() {
  // connect to the cobalt fidl service provided by the environment.
  fuchsia::cobalt::EncoderFactorySyncPtr factory;
  context_->ConnectToEnvironmentService(factory.NewRequest());

  fsl::SizedVmo config_vmo;
  bool success = fsl::VmoFromFilename(kConfigBinProtoPath, &config_vmo);
  FXL_CHECK(success) << "Could not read Cobalt config file into VMO";

  fuchsia::cobalt::ProjectProfile profile;
  fuchsia::mem::Buffer buf = std::move(config_vmo).ToTransport();
  profile.config.vmo = std::move(buf.vmo);
  profile.config.size = buf.size;

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  factory->GetEncoderForProject(std::move(profile), encoder_.NewRequest(),
                               &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "GetEncoderForProject() => " << StatusToString(status);
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  SystemMetricsApp app(kIntervalMinutes);
  app.Main();
  return 0;
}
