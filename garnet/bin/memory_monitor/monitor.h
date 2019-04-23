// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEMORY_MONITOR_MONITOR_H_
#define GARNET_BIN_MEMORY_MONITOR_MONITOR_H_

#include <fuchsia/memory/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/deprecated/object_dir.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/vmo.h>
#include <trace/observer.h>
#include <zircon/types.h>

#include <memory>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/macros.h"

namespace memory {

class Monitor : public fuchsia::memory::Monitor {
 public:
  explicit Monitor(std::unique_ptr<sys::ComponentContext> context,
                   const fxl::CommandLine& command_line,
                   async_dispatcher_t* dispatcher);
  ~Monitor();
  void Watch(fidl::InterfaceHandle<fuchsia::memory::Watcher> watcher) override;
  static const char kTraceName[];

 private:
  void UpdateState();

  void StartTracing();
  void StopTracing();

  void SampleAndPost();
  void PrintHelp();
  void Inspect(component::Object::ObjectVector* out_children);

  // Destroys a watcher proxy (called upon a connection error).
  void ReleaseWatcher(fuchsia::memory::Watcher* watcher);
  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(zx_info_kmem_stats_t stats);

  uint64_t prealloc_size_;
  zx::vmo prealloc_vmo_;
  bool logging_;
  bool tracing_;
  zx::duration delay_;
  zx_handle_t root_;
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  fidl::BindingSet<fuchsia::memory::Monitor> bindings_;
  std::vector<fuchsia::memory::WatcherPtr> watchers_;
  trace::TraceObserver trace_observer_;
  component::ObjectDir root_object_;
  fidl::BindingSet<fuchsia::inspect::Inspect> inspect_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Monitor);
};

}  // namespace memory

#endif  // GARNET_BIN_MEMORY_MONITOR_MONITOR_H_
