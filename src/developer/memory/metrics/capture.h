// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_METRICS_CAPTURE_H_
#define SRC_DEVELOPER_MEMORY_METRICS_CAPTURE_H_

#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <unordered_map>
#include <vector>

#include "src/lib/fxl/macros.h"

namespace memory {

struct Process {
  zx_koid_t koid;
  char name[ZX_MAX_NAME_LEN];
  std::vector<zx_koid_t> vmos;
};

struct Vmo {
  explicit Vmo(zx_info_vmo_t v)
      : koid(v.koid),
        parent_koid(v.parent_koid),
        committed_bytes(v.committed_bytes),
        allocated_bytes(v.size_bytes) {
    strncpy(name, v.name, sizeof(name));
  }
  zx_koid_t koid;
  char name[ZX_MAX_NAME_LEN];
  zx_koid_t parent_koid;
  uint64_t committed_bytes;
  uint64_t allocated_bytes;
  std::vector<zx_koid_t> children;
};

using CaptureLevel = enum { KMEM, PROCESS, VMO };

struct CaptureState {
  std::unique_ptr<fidl::WireSyncClient<fuchsia_kernel::Stats>> stats_client;
  zx_koid_t self_koid;
};

class OS {
 public:
  virtual zx_status_t GetKernelStats(
      std::unique_ptr<fidl::WireSyncClient<fuchsia_kernel::Stats>>* stats_client) = 0;
  virtual zx_handle_t ProcessSelf() = 0;
  virtual zx_time_t GetMonotonic() = 0;
  virtual zx_status_t GetProcesses(
      fit::function<zx_status_t(int /* depth */, zx_handle_t /* handle */, zx_koid_t /* koid */,
                                zx_koid_t /* parent_koid */)>
          cb) = 0;
  virtual zx_status_t GetProperty(zx_handle_t handle, uint32_t property, void* value,
                                  size_t name_len) = 0;
  virtual zx_status_t GetInfo(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                              size_t* actual, size_t* avail) = 0;
  virtual zx_status_t GetKernelMemoryStats(
      fidl::WireSyncClient<fuchsia_kernel::Stats>* stats_client, zx_info_kmem_stats_t* kmem) = 0;
  virtual zx_status_t GetKernelMemoryStatsExtended(
      fidl::WireSyncClient<fuchsia_kernel::Stats>* stats_client,
      zx_info_kmem_stats_extended_t* kmem_ext, zx_info_kmem_stats_t* kmem = nullptr) = 0;
};

class Capture {
 public:
  static const std::vector<std::string> kDefaultRootedVmoNames;
  static zx_status_t GetCaptureState(CaptureState* state);

  // Initialize a Capture instance. Be sure to call GetCapture prior to passing
  // the Capture instance to other systems (such as a Digest).
  //
  // Tip: This may require services (in your .cmx file) for
  //   fuchsia.kernel.RootJobForInspect and fuchsia.kernel.Stats, e.g.:
  //   "sandbox": {
  //       "services": [
  //           "fuchsia.kernel.RootJobForInspect",
  //           "fuchsia.kernel.Stats",
  //           ...
  static zx_status_t GetCapture(
      Capture* capture, const CaptureState& state, CaptureLevel level,
      const std::vector<std::string>& rooted_vmo_names = kDefaultRootedVmoNames);

  zx_time_t time() const { return time_; };
  const zx_info_kmem_stats_t& kmem() const { return kmem_; };
  const zx_info_kmem_stats_extended_t& kmem_extended() const { return kmem_extended_; };

  const std::unordered_map<zx_koid_t, Process>& koid_to_process() const { return koid_to_process_; }

  const std::unordered_map<zx_koid_t, Vmo>& koid_to_vmo() const { return koid_to_vmo_; }

  const Process& process_for_koid(zx_koid_t koid) const { return koid_to_process_.at(koid); }

  const Vmo& vmo_for_koid(zx_koid_t koid) const { return koid_to_vmo_.at(koid); }

 private:
  static zx_status_t GetCaptureState(CaptureState* state, OS* os);
  static zx_status_t GetCapture(Capture* capture, const CaptureState& state, CaptureLevel level,
                                OS* os, const std::vector<std::string>& rooted_vmo_names);
  void ReallocateDescendents(const std::vector<std::string>& rooted_vmo_names);
  void ReallocateDescendents(Vmo* parent);

  zx_time_t time_;
  zx_info_kmem_stats_t kmem_ = {};
  zx_info_kmem_stats_extended_t kmem_extended_ = {};
  std::unordered_map<zx_koid_t, Process> koid_to_process_;
  std::unordered_map<zx_koid_t, Vmo> koid_to_vmo_;
  std::vector<zx_koid_t> root_vmos_;

  class ProcessGetter;
  friend class TestUtils;
};

}  // namespace memory

#endif  // SRC_DEVELOPER_MEMORY_METRICS_CAPTURE_H_
