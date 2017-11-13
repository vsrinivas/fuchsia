// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/app.h"

#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <limits>

#include <zircon/device/cpu-trace/intel-pm.h>
#include <zircon/syscalls/log.h>
#include <trace-engine/instrumentation.h>
#include <trace-provider/provider.h>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/bin/cpuperf_provider/importer.h"
#include "garnet/bin/cpuperf_provider/reader.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

namespace cpuperf_provider {

namespace {

constexpr char kCpuPerfDev[] = "/dev/misc/cpu-trace";

fxl::UniqueFD OpenIpm() {
  int result = open(kCpuPerfDev, O_WRONLY);
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to open " << kCpuPerfDev << ": errno=" << errno;
  }
  return fxl::UniqueFD(result);  // take ownership here
}

bool IoctlIpmInit(int fd, bool sample_mode, uint32_t buffer_size) {
  ioctl_ipm_trace_config_t config;
  config.num_buffers = zx_system_get_num_cpus();
  if (sample_mode) {
    config.buffer_size = buffer_size;
  } else {
    // For "counting mode" we just need something large enough to hold
    // the header + zx_x86_ipm_counters_t.
    config.buffer_size = (sizeof(zx_x86_ipm_buffer_info_t) +
                          sizeof(zx_x86_ipm_counters_t));
  }
  FXL_VLOG(2) << fxl::StringPrintf("num_buffers=%u, buffer_size=0x%x",
                                   config.num_buffers, config.buffer_size);
  auto status = ioctl_ipm_alloc_trace(fd, &config);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "ioctl_ipm_alloc_trace failed: status=" << status;
  return status == ZX_OK;
}

void IoctlIpmFree(int fd, bool starting) {
  auto status = ioctl_ipm_free_trace(fd);
  if (status != ZX_OK) {
    // This can get called while tracing is currently stopped.
    if (starting && status == ZX_ERR_BAD_STATE) {
      ; // dont report an error in this case
    } else {
      FXL_LOG(ERROR) << "ioctl_ipm_free_trace failed: status=" << status;
    }
  }
}

bool IoctlIpmStart(int fd) {
  auto status = ioctl_ipm_start(fd);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "ioctl_ipm_start failed: status=" << status;
  return status == ZX_OK;
}

void IoctlIpmStop(int fd, bool starting) {
  auto status = ioctl_ipm_stop(fd);
  if (status != ZX_OK) {
    // This can get called while tracing is currently stopped.
    if (starting && status == ZX_ERR_BAD_STATE)
      ; // dont report an error in this case
    else
      FXL_LOG(ERROR) << "ioctl_ipm_stop failed: status=" << status;
  }
}

bool IoctlIpmStageSimpleConfig(int fd, uint32_t category_mask) {
  ioctl_ipm_simple_perf_config_t config;
  config.categories = category_mask;
  auto status = ioctl_ipm_stage_simple_perf_config(fd, &config);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "ioctl_ipm_stage_simple_perf_config failed: status=" << status;
  return status == ZX_OK;
}

// If only fxl string/number conversions supported 0x.

bool ParseNumber(const char* name, const fxl::StringView& arg,
                 uint64_t* value) {
  if (arg.size() > 2 &&
      arg[0] == '0' &&
      (arg[1] == 'x' || arg[1] == 'X')) {
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
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  if (command_line.HasOption("help")) {
    PrintHelp();
    exit(EXIT_SUCCESS);
  }

  std::string buffer_size_as_string;
  if (command_line.GetOptionValue("buffer-size", &buffer_size_as_string)) {
    uint64_t buffer_size;
    if (!ParseNumber("buffer-size", buffer_size_as_string, &buffer_size))
      exit(EXIT_FAILURE);
    uint64_t max_buffer_size = std::numeric_limits<uint32_t>::max();
    if (buffer_size > max_buffer_size) {
      FXL_LOG(ERROR) << "Buffer size too large, max " << max_buffer_size;
      exit(EXIT_FAILURE);
    }
    buffer_size_ = static_cast<uint32_t>(buffer_size);
  }

  trace_observer_.Start(fsl::MessageLoop::GetCurrent()->async(),
                        [this] { UpdateState(); });
}

App::~App() {}

void App::PrintHelp() {
  std::cout << "cpuperf_provider [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --help: Produce this help message" << std::endl;
  std::cout << "  --buffer-size=<size>: Trace data buffer size [default="
        << kDefaultBufferSize << "]" << std::endl;
}

uint32_t App::GetCategoryMask() {
  uint32_t mask = 0;
  for (size_t i = 0; i < GetNumCategories(); ++i) {
    auto& category = GetCategory(i);
    if (trace_is_category_enabled(category.name)) {
      FXL_VLOG(2) << "Category " << category.name << " enabled";
      if ((category.value & IPM_CATEGORY_PROGRAMMABLE_MASK) &&
          (mask & IPM_CATEGORY_PROGRAMMABLE_MASK)) {
        FXL_LOG(ERROR) << "Only one programmable category at a time is currenty supported";
        return 0;
      }
      mask |= category.value;
    }
  }

  // If neither OS,USER are specified, track both.
  if ((mask & (IPM_CATEGORY_OS | IPM_CATEGORY_USER)) == 0)
    mask |= IPM_CATEGORY_OS | IPM_CATEGORY_USER;

  return mask;
}

void App::UpdateState() {
  uint32_t category_mask = 0;

  if (trace_state() == TRACE_STARTED) {
    category_mask = GetCategoryMask();
  }

  if (current_category_mask_ != category_mask) {
    StopTracing();
    StartTracing(category_mask);
  }
}

void App::StartTracing(uint32_t category_mask) {
  FXL_DCHECK(!context_);
  if (!category_mask) {
    return;  // nothing to trace
  }

  auto fd = OpenIpm();
  if (!fd.is_valid()) {
    return;
  }

  FXL_VLOG(2) << "Starting trace, category_mask = 0x"
              << std::hex << category_mask;

  context_ = trace_acquire_context();
  if (!context_) {
    // Tracing was disabled in the meantime.
    return;
  }
  current_category_mask_ = category_mask;

  // First ensure any previous trace is gone.
  IoctlIpmStop(fd.get(), true);
  IoctlIpmFree(fd.get(), true);

  if (!IoctlIpmInit(fd.get(), (category_mask & IPM_CATEGORY_MODE_MASK) != 0,
                    buffer_size_))
    goto Fail;

  // Configure the device, telling it what data to collect.
  if (!IoctlIpmStageSimpleConfig(fd.get(), category_mask))
    goto Fail;

  start_time_ = zx_ticks_get();
  if (!IoctlIpmStart(fd.get()))
    goto Fail;

  FXL_LOG(INFO) << "Started tracing";
  return;

Fail:
  trace_release_context(context_);
  context_ = nullptr;
  current_category_mask_ = 0u;
}

void App::StopTracing() {
  if (!context_) {
    return;  // not currently tracing
  }
  FXL_DCHECK(current_category_mask_);

  FXL_LOG(INFO) << "Stopping trace";

  fxl::UniqueFD fd = OpenIpm();
  if (fd.is_valid()) {
    IoctlIpmStop(fd.get(), false);
  }

  stop_time_ = zx_ticks_get();

  Reader reader(fd.get(), buffer_size_);
  if (reader.is_valid()) {
    Importer importer(context_, current_category_mask_,
                      start_time_, stop_time_);
    if (!importer.Import(reader)) {
      FXL_LOG(ERROR) << "Errors encountered while importing cpuperf data";
    }
  } else {
    FXL_LOG(ERROR) << "Unable to initialize reader";
  }

  if (fd.is_valid()) {
    IoctlIpmFree(fd.get(), false);
  }

  trace_release_context(context_);
  context_ = nullptr;
  current_category_mask_ = 0u;
}

}  // namespace cpuperf_provider
