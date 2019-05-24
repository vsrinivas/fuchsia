// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/memory_monitor/monitor.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <iostream>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <string.h>
#include <trace/event.h>
#include <zircon/status.h>


namespace memory {

namespace {

constexpr char kKstatsPathComponent[] = "kstats";

}  // namespace

const char Monitor::kTraceName[] = "memory_monitor";

Monitor::Monitor(std::unique_ptr<sys::ComponentContext> context,
                 const fxl::CommandLine& command_line,
                 async_dispatcher_t* dispatcher)
    : prealloc_size_(0),
      logging_(command_line.HasOption("log")),
      tracing_(false),
      delay_(zx::sec(1)),
      dispatcher_(dispatcher),
      component_context_(std::move(context)),
      root_object_(component::ObjectDir::Make("root")) {
  auto s = Capture::GetCaptureState(capture_state_);
  if (s != ZX_OK) {
    FXL_LOG(ERROR) << "Error getting capture state: "
                   << zx_status_get_string(s);
    exit(EXIT_FAILURE);
  }

  component_context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  root_object_.set_children_callback(
      {kKstatsPathComponent},
      [this](component::Object::ObjectVector* out_children) {
        Inspect(out_children);
      });
  component_context_->outgoing()->GetOrCreateDirectory("objects")->AddEntry(
      fuchsia::inspect::Inspect::Name_,
      std::make_unique<vfs::Service>(
          inspect_bindings_.GetHandler(root_object_.object().get())));

  if (command_line.HasOption("help")) {
    PrintHelp();
    exit(EXIT_SUCCESS);
  }
  std::string delay_as_string;
  if (command_line.GetOptionValue("delay", &delay_as_string)) {
    unsigned delay_as_int;
    if (!fxl::StringToNumberWithError<unsigned>(delay_as_string,
                                                &delay_as_int)) {
      FXL_LOG(ERROR) << "Invalid value for delay: " << delay_as_string;
      exit(-1);
    }
    delay_ = zx::msec(delay_as_int);
  }
  std::string prealloc_as_string;
  if (command_line.GetOptionValue("prealloc", &prealloc_as_string)) {
    FXL_LOG(INFO) << "prealloc_string: " << prealloc_as_string;
    if (!fxl::StringToNumberWithError<uint64_t>(prealloc_as_string,
                                                &prealloc_size_)) {
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
                                        ZX_VM_PERM_READ, &prealloc_addr);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::vmar::map() returns "
                     << zx_status_get_string(status);
      exit(-1);
    }

    status =
        prealloc_vmo_.op_range(ZX_VMO_OP_COMMIT, 0, prealloc_size_, NULL, 0);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::vmo::op_range() returns "
                     << zx_status_get_string(status);
      exit(-1);
    }
  }

  trace_observer_.Start(dispatcher_, [this] { UpdateState(); });
  if (logging_) {
    Capture capture;
    auto s = Capture::GetCapture(capture, capture_state_, KMEM);
    if (s != ZX_OK) {
      FXL_LOG(ERROR) << "Error getting capture: " << zx_status_get_string(s);
      exit(EXIT_FAILURE);
    }
    auto const& kmem = capture.kmem();
    FXL_LOG(INFO) << "Total: " << kmem.total_bytes
                  << " Wired: " << kmem.wired_bytes
                  << " Total Heap: " << kmem.total_heap_bytes;
  }
  SampleAndPost();
}

Monitor::~Monitor() {
  // TODO(CF-257).
  root_object_.set_children_callback({kKstatsPathComponent}, nullptr);
}

void Monitor::Watch(fidl::InterfaceHandle<fuchsia::memory::Watcher> watcher) {
  fuchsia::memory::WatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::memory::Watcher* proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_error_handler([this, proxy_raw_ptr](zx_status_t status) {
    ReleaseWatcher(proxy_raw_ptr);
  });
  watchers_.push_back(std::move(watcher_proxy));
  SampleAndPost();
}

void Monitor::ReleaseWatcher(fuchsia::memory::Watcher* watcher) {
  auto predicate = [watcher](const auto& target) {
    return target.get() == watcher;
  };
  watchers_.erase(
      std::remove_if(watchers_.begin(), watchers_.end(), predicate));
}

void Monitor::NotifyWatchers(const zx_info_kmem_stats_t& kmem_stats) {
  fuchsia::memory::Stats stats{
      .total_bytes = kmem_stats.total_bytes,
      .free_bytes = kmem_stats.free_bytes,
      .wired_bytes = kmem_stats.wired_bytes,
      .total_heap_bytes = kmem_stats.total_heap_bytes,
      .free_heap_bytes = kmem_stats.free_heap_bytes,
      .vmo_bytes = kmem_stats.vmo_bytes,
      .mmu_overhead_bytes = kmem_stats.mmu_overhead_bytes,
      .ipc_bytes = kmem_stats.ipc_bytes,
      .other_bytes = kmem_stats.other_bytes,
  };

  for (auto& watcher : watchers_) {
    watcher->OnChange(stats);
  }
}

void Monitor::PrintHelp() {
  std::cout << "memory_monitor [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --log" << std::endl;
  std::cout << "  --prealloc=kbytes" << std::endl;
  std::cout << "  --delay=msecs" << std::endl;
}

void Monitor::Inspect(component::Object::ObjectVector* out_children) {
  auto kstats = component::ObjectDir::Make(kKstatsPathComponent);
  Capture capture;
  auto s = Capture::GetCapture(capture, capture_state_, KMEM);
  if (s != ZX_OK) {
    FXL_LOG(ERROR) << "Error getting capture: " << zx_status_get_string(s);
  }
  auto const& kmem = capture.kmem();

  kstats.set_metric("total_bytes", component::UIntMetric(kmem.total_bytes));
  kstats.set_metric("free_bytes", component::UIntMetric(kmem.free_bytes));
  kstats.set_metric("wired_bytes", component::UIntMetric(kmem.wired_bytes));
  kstats.set_metric(
      "total_heap_bytes",component::UIntMetric(kmem.total_heap_bytes));
  kstats.set_metric("vmo_bytes", component::UIntMetric(kmem.vmo_bytes));
  kstats.set_metric(
      "mmu_overhead_bytes", component::UIntMetric(kmem.mmu_overhead_bytes));
  kstats.set_metric("ipc_bytes", component::UIntMetric(kmem.ipc_bytes));
  kstats.set_metric("other_bytes", component::UIntMetric(kmem.other_bytes));
  out_children->push_back(kstats.object());
}

void Monitor::SampleAndPost() {
  if (logging_ || tracing_ || watchers_.size() > 0) {
    Capture capture;
    auto s = Capture::GetCapture(capture, capture_state_, KMEM);
    if (s != ZX_OK) {
      FXL_LOG(ERROR) << "Error getting capture: " << zx_status_get_string(s);
      return;
    }
    auto const& kmem = capture.kmem();
    if (logging_) {
      FXL_LOG(INFO) << "Free: " << kmem.free_bytes
                    << " Free Heap: " << kmem.free_heap_bytes
                    << " VMO: " << kmem.vmo_bytes
                    << " MMU: " << kmem.mmu_overhead_bytes
                    << " IPC: " << kmem.ipc_bytes;
    }
    if (tracing_) {
      TRACE_COUNTER(kTraceName, "allocated", 0, "vmo", kmem.vmo_bytes,
                    "mmu_overhead", kmem.mmu_overhead_bytes, "ipc",
                    kmem.ipc_bytes);
      TRACE_COUNTER(kTraceName, "free", 0, "free", kmem.free_bytes,
                    "free_heap", kmem.free_heap_bytes);
    }
    NotifyWatchers(kmem);
    async::PostDelayedTask(
        dispatcher_, [this] { SampleAndPost(); }, delay_);
  }
}

void Monitor::UpdateState() {
  if (trace_state() == TRACE_STARTED) {
    if (trace_is_category_enabled(kTraceName)) {
      FXL_LOG(INFO) << "Tracing started";
      if (!tracing_) {
        Capture capture;
        auto s = Capture::GetCapture(capture, capture_state_, KMEM);
        if (s != ZX_OK) {
          FXL_LOG(ERROR) << "Error getting capture: "
                         << zx_status_get_string(s);
          return;
        }
        auto const& kmem = capture.kmem();
        TRACE_COUNTER(kTraceName,
                      "fixed", 0,
                      "total", kmem.total_bytes,
                      "wired", kmem.wired_bytes,
                      "total_heap", kmem.total_heap_bytes);
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

}  // namespace memory
