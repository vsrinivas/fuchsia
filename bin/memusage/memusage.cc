// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/memusage/memusage.h"

#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <string.h>

#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/async/default.h>
#include <lib/async/cpp/time.h>
#include <lib/fdio/util.h>
#include <trace/event.h>
#include <zircon/status.h>

#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"

namespace memusage {

namespace {

zx_status_t get_root_resource(zx_handle_t* root_resource) {
  const char* sysinfo = "/dev/misc/sysinfo";
  int fd = open(sysinfo, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Cannot open sysinfo: " << strerror(errno);
    return ZX_ERR_NOT_FOUND;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain sysinfo channel: "
                   << zx_status_get_string(status);
    return status;
  }

  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, root_resource);
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain root resource: "
                   << zx_status_get_string(fidl_status);
    return fidl_status;
  } else if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain root resource: "
                   << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}

}  // namespace

App::App(const fxl::CommandLine& command_line, async_dispatcher_t* dispatcher)
    : prealloc_size_(0),
      logging_(command_line.HasOption("log")),
      tracing_(false),
      delay_(zx::sec(1)),
      startup_context_(component::StartupContext::CreateFromStartupInfo()) {
  auto status = get_root_resource(&root_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error getting root_resource: " << status;
    exit(-1);
  }
  if (command_line.HasOption("help")) {
    PrintHelp();
    exit(EXIT_SUCCESS);
  }
  std::string delay_as_string;
  if (command_line.GetOptionValue("delay", &delay_as_string)) {
    unsigned delay_as_int;
    if (!fxl::StringToNumberWithError<unsigned>(delay_as_string, &delay_as_int)) {
      FXL_LOG(ERROR) << "Invalid value for delay: " << delay_as_string;
      exit(-1);
    }
    delay_ = zx::msec(delay_as_int);
  }
  std::string prealloc_as_string;
  if (command_line.GetOptionValue("prealloc", &prealloc_as_string)) {
    FXL_LOG(INFO) << "prealloc_string: " << prealloc_as_string;
    if (!fxl::StringToNumberWithError<uint64_t>(
          prealloc_as_string, &prealloc_size_)) {
      FXL_LOG(ERROR) << "Invalid value for prealloc: " << prealloc_as_string;
      exit(-1);
    }
    prealloc_size_ *= (1024 * 1024);
    auto status = zx::vmo::create(prealloc_size_, 0, &prealloc_vmo_);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "zx::vmo::create() returns "
                       << zx_status_get_string(status);
        exit(-1);
    }
    prealloc_vmo_.get_size(&prealloc_size_);
    uintptr_t prealloc_addr = 0;
    status = zx::vmar::root_self()->map(0, prealloc_vmo_, 0, prealloc_size_,
                                        ZX_VM_FLAG_PERM_READ,
                                        &prealloc_addr);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "zx::vmar::map() returns "
                       << zx_status_get_string(status);
        exit(-1);
    }

    status = prealloc_vmo_.op_range(
        ZX_VMO_OP_COMMIT, 0, prealloc_size_, NULL, 0);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "zx::vmo::op_range() returns "
                       << zx_status_get_string(status);
        exit(-1);
    }
  }

  trace_observer_.Start(dispatcher, [this] { UpdateState(); });
  if (logging_) {
    zx_info_kmem_stats_t stats;
    zx_status_t err = zx_object_get_info(
        root_, ZX_INFO_KMEM_STATS, &stats, sizeof(stats), NULL, NULL);
    if (err != ZX_OK) {
        FXL_LOG(ERROR) << "ZX_INFO_KMEM_STATS returns "
                       << zx_status_get_string(err);
        exit(-1);
    }
    FXL_LOG(INFO) << "Total: " << stats.total_bytes
                  << " Wired: " << stats.wired_bytes
                  << " Total Heap: " << stats.total_heap_bytes;
  }
  SampleAndPost();
}

App::~App() {}

void App::PrintHelp() {
  std::cout << "memusage [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --log" << std::endl;
  std::cout << "  --prealloc=kbytes" << std::endl;
  std::cout << "  --delay=msecs" << std::endl;
}

void App::SampleAndPost() {
  if (logging_ || tracing_) {
    zx_info_kmem_stats_t stats;
    zx_status_t err = zx_object_get_info(
        root_, ZX_INFO_KMEM_STATS, &stats, sizeof(stats), NULL, NULL);
    if (err != ZX_OK) {
        FXL_LOG(ERROR) << "ZX_INFO_KMEM_STATS returns "
                       << zx_status_get_string(err);
        return;
    }
    if (logging_) {
      FXL_LOG(INFO) << "Free: " << stats.free_bytes
                    << " Free Heap: " << stats.free_heap_bytes
                    << " VMO: " << stats.vmo_bytes
                    << " MMU: " << stats.mmu_overhead_bytes
                    << " IPC: " << stats.ipc_bytes;
    }
    if (tracing_) {
      TRACE_COUNTER("memusage", "allocated", 0,
                    "vmo", stats.vmo_bytes,
                    "mmu_overhead", stats.mmu_overhead_bytes,
                    "ipc", stats.ipc_bytes);
      TRACE_COUNTER("memusage", "free", 0,
                    "free", stats.free_bytes,
                    "free_heap", stats.free_heap_bytes);
    }
    async::PostDelayedTask(async_get_default_dispatcher(),
        [this] { SampleAndPost(); }, delay_);
  }
}

void App::UpdateState() {
  if (trace_state() == TRACE_STARTED) {
    if (trace_is_category_enabled("memusage")) {
      FXL_LOG(INFO) << "Tracing started";
      if (!tracing_) {
        zx_info_kmem_stats_t stats;
        zx_status_t err = zx_object_get_info(
            root_, ZX_INFO_KMEM_STATS, &stats, sizeof(stats), NULL, NULL);
        if (err != ZX_OK) {
            FXL_LOG(ERROR) << "ZX_INFO_KMEM_STATS returns "
                          << zx_status_get_string(err);
            return;
        }
        TRACE_COUNTER("memusage", "fixed", 0,
                      "total", stats.total_bytes,
                      "wired", stats.wired_bytes,
                      "total_heap", stats.total_heap_bytes);
        tracing_ = true;
        if (!logging_) {
          SampleAndPost();
        }
      }
    }
  } else {
    if (tracing_) {
      FXL_LOG(INFO) << "Tracing stopped";
      tracing_ = false;
    }
  }
}

}  // namespace memusage
