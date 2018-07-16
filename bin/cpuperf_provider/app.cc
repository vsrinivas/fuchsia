// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/app.h"

#include <iostream>
#include <limits>
#include <memory>

#include <lib/async/default.h>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/bin/cpuperf_provider/importer.h"
#include "garnet/lib/cpuperf/controller.h"
#include "garnet/lib/cpuperf/reader.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"

namespace cpuperf_provider {

namespace {

// If only fxl string/number conversions supported 0x.

bool ParseNumber(const char* name, const fxl::StringView& arg,
                 uint64_t* value) {
  if (arg.size() > 2 && arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
    if (!fxl::StringToNumberWithError<uint64_t>(arg.substr(2), value,
                                                fxl::Base::k16)) {
      FXL_LOG(ERROR) << "Invalid value for " << name << ": " << arg;
      return false;
    }
  } else {
    if (!fxl::StringToNumberWithError<uint64_t>(arg, value)) {
      FXL_LOG(ERROR) << "Invalid value for " << name << ": " << arg;
      return false;
    }
  }
  return true;
}

}  // namespace

App::App(const fxl::CommandLine& command_line)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()) {
  if (command_line.HasOption("help")) {
    PrintHelp();
    exit(EXIT_SUCCESS);
  }

  std::string buffer_size_as_string;
  if (command_line.GetOptionValue("buffer-size", &buffer_size_as_string)) {
    uint64_t buffer_size;
    if (!ParseNumber("buffer-size", buffer_size_as_string, &buffer_size))
      exit(EXIT_FAILURE);
    if (buffer_size == 0) {
      FXL_LOG(ERROR) << "Buffer size cannot be zero";
      exit(EXIT_FAILURE);
    }
    if (buffer_size > kMaxBufferSizeInMb) {
      FXL_LOG(ERROR) << "Buffer size too large, max " << kMaxBufferSizeInMb;
      exit(EXIT_FAILURE);
    }
    buffer_size_in_mb_ = static_cast<uint32_t>(buffer_size);
  }

  trace_observer_.Start(async_get_default_dispatcher(), [this] { UpdateState(); });
}

App::~App() {}

void App::PrintHelp() {
  std::cout << "cpuperf_provider [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --help: Produce this help message" << std::endl;
  std::cout << "  --buffer-size=<size>: Trace data buffer size (MB) [default="
            << kDefaultBufferSizeInMb << "]" << std::endl;
}

void App::UpdateState() {
  if (trace_state() == TRACE_STARTED) {
    TraceConfig config;
    config.Update();
    if (trace_config_.Changed(config)) {
      StopTracing();
      if (config.is_enabled())
        StartTracing(config);
    }
  } else {
    StopTracing();
  }
}

void App::StartTracing(const TraceConfig& trace_config) {
  FXL_DCHECK(trace_config.is_enabled());
  FXL_DCHECK(!context_);
  FXL_DCHECK(!controller_);

  cpuperf_config_t device_config;
  if (!trace_config.TranslateToDeviceConfig(&device_config)) {
    FXL_LOG(ERROR) << "Error converting trace config to device config";
    return;
  }

  auto controller = std::unique_ptr<cpuperf::Controller>(
      new cpuperf::Controller(buffer_size_in_mb_, device_config));
  if (!controller->is_valid()) {
    FXL_LOG(ERROR) << "Cpuperf controller failed to initialize";
    return;
  }

  FXL_VLOG(1) << "Starting trace, config = " << trace_config.ToString();

  context_ = trace_acquire_context();
  if (!context_) {
    // Tracing was disabled in the meantime.
    return;
  }

  start_time_ = zx_ticks_get();
  if (!controller->Start())
    goto Fail;

  FXL_LOG(INFO) << "Started tracing";
  trace_config_ = trace_config;
  controller_.reset(controller.release());
  return;

Fail:
  trace_release_context(context_);
  context_ = nullptr;
}

void App::StopTracing() {
  if (!context_) {
    return;  // not currently tracing
  }
  FXL_DCHECK(trace_config_.is_enabled());

  FXL_LOG(INFO) << "Stopping trace";

  controller_->Stop();

  stop_time_ = zx_ticks_get();

  auto reader = controller_->GetReader();
  if (reader->is_valid()) {
    Importer importer(context_, &trace_config_, start_time_, stop_time_);
    if (!importer.Import(*reader)) {
      FXL_LOG(ERROR) << "Errors encountered while importing cpuperf data";
    }
  } else {
    FXL_LOG(ERROR) << "Unable to initialize reader";
  }

  trace_release_context(context_);
  context_ = nullptr;
  trace_config_.Reset();
  controller_.reset();
}

}  // namespace cpuperf_provider
