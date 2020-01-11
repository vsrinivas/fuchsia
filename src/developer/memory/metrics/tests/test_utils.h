// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_METRICS_TESTS_TEST_UTILS_H_
#define SRC_DEVELOPER_MEMORY_METRICS_TESTS_TEST_UTILS_H_

#include <vector>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/summary.h"

namespace memory {

struct CaptureTemplate {
  zx_time_t time;
  zx_info_kmem_stats_t kmem;
  std::vector<zx_info_vmo_t> vmos;
  std::vector<Process> processes;
  std::vector<std::string> rooted_vmo_names;
};

struct GetProcessesCallback {
  int depth;
  zx_handle_t handle;
  zx_koid_t koid;
  zx_koid_t parent_koid;
};

struct GetProcessesResponse {
  zx_status_t ret;
  std::vector<GetProcessesCallback> callbacks;
};

struct GetPropertyResponse {
  zx_handle_t handle;
  uint32_t property;
  const void* value;
  size_t value_len;
  zx_status_t ret;
};

struct GetInfoResponse {
  zx_handle_t handle;
  uint32_t topic;
  const void* values;
  size_t value_size;
  size_t value_count;
  zx_status_t ret;
};

struct OsResponses {
  const std::vector<GetProcessesResponse> get_processes;
  const std::vector<GetPropertyResponse> get_property;
  const std::vector<GetInfoResponse> get_info;
};

class CaptureSupplier {
 public:
  explicit CaptureSupplier(std::vector<CaptureTemplate> templates)
      : templates_(std::move(templates)), index_(0) {}

  zx_status_t GetCapture(Capture* capture, CaptureLevel level,
                         bool use_capture_supplier_time = false);
  bool empty() const { return index_ == templates_.size(); }

 private:
  std::vector<CaptureTemplate> templates_;
  size_t index_;
};

class TestUtils {
 public:
  const static zx_handle_t kRootHandle;
  const static zx_handle_t kSelfHandle;
  const static zx_koid_t kSelfKoid;

  static void CreateCapture(Capture* capture, const CaptureTemplate& t, CaptureLevel level = VMO);
  static zx_status_t GetCapture(Capture* capture, CaptureLevel level, const OsResponses& r);

  // Sorted by koid.
  static std::vector<ProcessSummary> GetProcessSummaries(const Summary& summary);
};

}  // namespace memory

#endif  // SRC_DEVELOPER_MEMORY_METRICS_TESTS_TEST_UTILS_H_
