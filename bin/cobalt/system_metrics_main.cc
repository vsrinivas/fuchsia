// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.

#include <chrono>
#include <memory>
#include <thread>

#include "lib/app/cpp/application_context.h"
#include "lib/cobalt/fidl/cobalt.fidl-sync.h"
#include "lib/cobalt/fidl/cobalt.fidl.h"
#include "lib/fsl/tasks/message_loop.h"

const uint32_t kSystemMetricsProjectId = 102;
const uint32_t kUptimeMetricId = 1;
const uint32_t kRawEncodingId = 1;
const unsigned int kIntervalMinutes = 1;

std::string StatusToString(cobalt::Status status) {
  switch (status) {
    case cobalt::Status::OK:
      return "OK";
    case cobalt::Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case cobalt::Status::OBSERVATION_TOO_BIG:
      return "OBSERVATION_TOO_BIG";
    case cobalt::Status::TEMPORARILY_FULL:
      return "TEMPORARILY_FULL";
    case cobalt::Status::SEND_FAILED:
      return "SEND_FAILED";
    case cobalt::Status::FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case cobalt::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
};

class SystemMetricsApp {
 public:
  // tick_interval_minutes is the number of minutes to sleep in between calls to
  // the GatherMetrics method.
  SystemMetricsApp(unsigned int tick_interval_minutes)
      : context_(app::ApplicationContext::CreateFromStartupInfo()),
        start_time_(std::chrono::steady_clock::now()),
        tick_interval_(tick_interval_minutes) {}

  // Main is invoked to initialize the app and start the metric gathering loop.
  void Main();

 private:
  void ConnectToEnvironmentService();

  void GatherMetrics();

  // LogUptime returns the status returned by its last call to Add*Observation.
  cobalt::Status LogUptime(std::chrono::minutes uptime_minutes);

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  cobalt::CobaltEncoderSyncPtr encoder_;
  std::chrono::steady_clock::time_point start_time_;
  std::chrono::minutes tick_interval_;
  // We don't log every minute of uptime. We log in exponentially-growing
  // increments. This keeps track of which minute should be logged.
  int next_uptime_bucket_ = 0;
};

void SystemMetricsApp::GatherMetrics() {
  auto now = std::chrono::steady_clock::now();
  auto uptime = now - start_time_;
  auto uptime_minutes =
      std::chrono::duration_cast<std::chrono::minutes>(uptime);

  LogUptime(uptime_minutes);
}

cobalt::Status SystemMetricsApp::LogUptime(std::chrono::minutes uptime_minutes) {
  while (next_uptime_bucket_ <= uptime_minutes.count()) {
    cobalt::Status status = cobalt::Status::INTERNAL_ERROR;

    encoder_->AddIntObservation(kUptimeMetricId, kRawEncodingId,
                                next_uptime_bucket_, &status);
    // If we failed to send an observation, we stop gathering metrics for up to
    // one minute.
    if (status != cobalt::Status::OK) {
      FXL_LOG(ERROR) << "AddIntObservation() => " << StatusToString(status);
      return status;
    }

    if (next_uptime_bucket_ == 0) {
      next_uptime_bucket_ = 1;
    } else {
      next_uptime_bucket_ *= 2;
    }
  }

  return cobalt::Status::OK;
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
  // Connect to the Cobalt FIDL service provided by the environment.
  cobalt::CobaltEncoderFactorySyncPtr factory;
  context_->ConnectToEnvironmentService(fidl::GetSynchronousProxy(&factory));
  factory->GetEncoder(kSystemMetricsProjectId, GetSynchronousProxy(&encoder_));
}

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  SystemMetricsApp app(kIntervalMinutes);
  app.Main();
  return 0;
}
