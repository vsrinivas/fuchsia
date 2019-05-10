// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/app.h"

#include <iostream>
#include <limits>
#include <memory>

#include <lib/async/default.h>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_number_conversions.h>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/bin/cpuperf_provider/importer.h"
#include "garnet/lib/perfmon/controller.h"
#include "garnet/lib/perfmon/reader.h"

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

bool GetBufferSizeInPages(uint32_t size_in_mb, uint32_t* out_num_pages) {
  const uint64_t kPagesPerMb = 1024 * 1024 / perfmon::Controller::kPageSize;
  const uint64_t kMaxSizeInMb =
      std::numeric_limits<uint32_t>::max() / kPagesPerMb;
  if (size_in_mb > kMaxSizeInMb) {
    return false;
  }
  *out_num_pages = size_in_mb * kPagesPerMb;
  return true;
}

}  // namespace

App::App(const fxl::CommandLine& command_line)
    : startup_context_(sys::ComponentContext::Create()) {
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
    // The provided buffer size is in MB, the controller takes the buffer size
    // in pages.
    uint32_t buffer_size_in_pages;
    if (!GetBufferSizeInPages(buffer_size, &buffer_size_in_pages)) {
      FXL_LOG(ERROR) << "Buffer size too large";
      exit(EXIT_FAILURE);
    }
    buffer_size_in_pages_ = buffer_size_in_pages;
  }

  // The supported models and their names are determined by lib/perfmon.
  // These are defaults for now.
  model_event_manager_ = perfmon::ModelEventManager::Create(
    perfmon::GetDefaultModelName());
  FXL_CHECK(model_event_manager_);

  trace_observer_.Start(async_get_default_dispatcher(),
                        [this] { UpdateState(); });
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
    auto new_config = TraceConfig::Create(model_event_manager_.get(),
                                          trace_is_category_enabled);
    if (new_config && trace_config_->Changed(*new_config)) {
      StopTracing();
      if (new_config->is_enabled()) {
        StartTracing(std::move(new_config));
      }
    }
  } else {
    StopTracing();
  }
}

void App::StartTracing(std::unique_ptr<TraceConfig> trace_config) {
  FXL_DCHECK(trace_config->is_enabled());
  FXL_DCHECK(!context_);
  FXL_DCHECK(!controller_);

  perfmon::Config device_config;
  if (!trace_config->TranslateToDeviceConfig(&device_config)) {
    FXL_LOG(ERROR) << "Error converting trace config to device config";
    return;
  }

  std::unique_ptr<perfmon::Controller> controller;
  if (!perfmon::Controller::Create(buffer_size_in_pages_, device_config,
                                   &controller)) {
    FXL_LOG(ERROR) << "Perfmon controller failed to initialize";
    return;
  }

  context_ = trace_acquire_prolonged_context();
  if (!context_) {
    // Tracing was disabled in the meantime.
    return;
  }

  FXL_VLOG(1) << "Starting trace, config = " << trace_config->ToString();

  start_time_ = zx_ticks_get();
  if (!controller->Start())
    goto Fail;

  FXL_LOG(INFO) << "Started tracing";
  trace_config_ = std::move(trace_config);
  controller_.reset(controller.release());
  return;

Fail:
  trace_release_prolonged_context(context_);
  context_ = nullptr;
}

void App::StopTracing() {
  if (!context_) {
    return;  // not currently tracing
  }
  FXL_DCHECK(trace_config_->is_enabled());

  FXL_LOG(INFO) << "Stopping trace";

  controller_->Stop();

  stop_time_ = zx_ticks_get();

  // Acquire a context for writing to the trace buffer.
  auto buffer_context = trace_acquire_context();

  auto reader = controller_->GetReader();
  if (reader) {
    Importer importer(buffer_context, trace_config_.get(),
                      start_time_, stop_time_);
    const perfmon::Config& config = controller_->config();
    if (!importer.Import(*reader, config)) {
      FXL_LOG(ERROR) << "Errors encountered while importing perfmon data";
    }
  } else {
    FXL_LOG(ERROR) << "Unable to initialize reader";
  }

  trace_release_context(buffer_context);
  trace_release_prolonged_context(context_);
  context_ = nullptr;
  trace_config_.reset();
  controller_.reset();
}

}  // namespace cpuperf_provider
