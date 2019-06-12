// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEMORY_MONITOR_CAPTURE_H_
#define GARNET_BIN_MEMORY_MONITOR_CAPTURE_H_

#include <src/lib/fxl/macros.h>
#include <lib/zx/time.h>
#include <unordered_map>
#include <vector>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace memory {

struct Process {
  zx_koid_t koid;
  char name[ZX_MAX_NAME_LEN];
  zx_info_task_stats_t stats;
  std::vector<zx_koid_t> vmos;
};

typedef enum {KMEM, PROCESS, VMO} CaptureLevel;

struct CaptureState {
  zx_handle_t root;
  zx_koid_t self_koid;
};

class Capture {
 public:
  static zx_status_t GetCaptureState(CaptureState& state);
  static zx_status_t GetCapture(
      Capture& capture, const CaptureState& state, CaptureLevel level);

  zx_time_t time() const { return time_; };
  const zx_info_kmem_stats_t& kmem() const { return kmem_; } ;

  const std::unordered_map<zx_koid_t, const Process>& koid_to_process() const {
    return koid_to_process_;
  }

  const std::unordered_map<zx_koid_t, const zx_info_vmo_t>& 
      koid_to_vmo() const {
    return koid_to_vmo_;
  }

  const Process& process_for_koid(zx_koid_t koid) const {
    return koid_to_process_.at(koid);
  }

  const zx_info_vmo_t& vmo_for_koid(zx_koid_t koid) const {
    return koid_to_vmo_.at(koid);
  }

 private:
  zx_time_t time_;
  zx_info_kmem_stats_t kmem_;
  std::unordered_map<zx_koid_t, const Process> koid_to_process_;
  std::unordered_map<zx_koid_t, const zx_info_vmo_t> koid_to_vmo_;

  class ProcessGetter;
  friend class TestUtils;
};

}  // namespace memory

#endif  // GARNET_BIN_MEMORY_MONITOR_CAPTURE_H_
