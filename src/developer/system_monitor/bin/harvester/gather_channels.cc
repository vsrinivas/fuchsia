// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_channels.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <string>

#include <task-utils/walker.h>

#include "sample_bundle.h"
#include "task_tree.h"

// What is the verbose output level for trivia in this file. For easy debugging,
// change this value to 0 temporarily.
#define VERBOSE_FOR_FILE (3)

namespace harvester {

namespace {

// Utilities to create a SampleBundle with channel information.
class SampleBundleBuilder final {
 public:
  explicit SampleBundleBuilder(SampleBundle* samples)
      : sample_bundle_(samples) {}

  // Helper to add a value to the sample |int_sample_list_|.
  void AddKoidValue(zx_koid_t koid, const std::string& path,
                    dockyard::SampleValue value);

  // Gather channel information for a process.
  // |koid| must refer to the same process as the process handle.
  // |info| is a scratch buffer (it's not an input or output parameter).
  void AddProcessChannels(zx_handle_t process, zx_koid_t koid,
                          std::vector<zx_info_handle_extended>* info);

 private:
  SampleBundle* sample_bundle_;

  SampleBundleBuilder() = delete;
};

// Add a value to the samples.
void SampleBundleBuilder::AddKoidValue(zx_koid_t koid, const std::string& path,
                                       dockyard::SampleValue value) {
  sample_bundle_->AddIntSample("koid", koid, path, value);
}

void SampleBundleBuilder::AddProcessChannels(
    zx_handle_t process, zx_koid_t koid,
    std::vector<zx_info_handle_extended>* info) {
  for (;;) {
    size_t actual = info->size();
    size_t available = 0;
    zx_status_t status = zx_object_get_info(
        process, ZX_INFO_HANDLE_TABLE, info->data(),
        info->size() * sizeof(zx_info_handle_extended), &actual, &available);
    if (status != ZX_OK) {
      FX_VLOGS(VERBOSE_FOR_FILE)
          << ZxErrorString("AddKernelObjectsStats", status) << " for koid "
          << koid;
      return;
    }
    if (actual < available) {
      auto new_size = available;

      // Optimization: it's not required that the size double, but this is
      // likely to reduce the repeated calls to zx_object_get_info().
      new_size *= 2;

      info->resize(new_size);
      continue;
    }
    for (size_t i = 0; i < actual; ++i) {
      auto entry = (*info)[i];
      if (entry.type == ZX_OBJ_TYPE_CHANNEL) {
        // TODO(fxbug.dev/54364): add channel information when dockyard supports
        // multi maps. AddKoidValue(koid, "channel", entry.koid);
        AddKoidValue(entry.koid, "type", dockyard::KoidType::CHANNEL);
        AddKoidValue(entry.koid, "process", koid);
        AddKoidValue(entry.koid, "peer", entry.related_koid);
      }
    }
    break;
  }
}

}  // namespace

void AddChannels(SampleBundle* samples,
                 const std::vector<TaskTree::Task>& tasks) {
  SampleBundleBuilder builder(samples);
  std::vector<zx_info_handle_extended> buffer;

  // Optimization: This initial allocation is not required, but this can reduce
  // the number of retries by starting with something reasonable.
  buffer.resize(100);

  for (const auto& task : tasks) {
    builder.AddProcessChannels(task.handle, task.koid, &buffer);
  }
}

void GatherChannels::Gather() {
  SampleBundle samples;
  g_slow_data_task_tree.Gather();
  AddChannels(&samples, g_slow_data_task_tree.Processes());
  samples.Upload(DockyardPtr());
}

}  // namespace harvester
