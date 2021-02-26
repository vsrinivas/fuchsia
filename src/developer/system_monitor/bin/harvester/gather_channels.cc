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

 private:
  SampleBundle* sample_bundle_;

  SampleBundleBuilder() = delete;
};

// Add a value to the samples.
void SampleBundleBuilder::AddKoidValue(zx_koid_t koid, const std::string& path,
                                       dockyard::SampleValue value) {
  sample_bundle_->AddIntSample("koid", koid, path, value);
}

}  // namespace

void AddChannels(SampleBundle* samples,
                 const std::vector<TaskTree::Task>& processes, OS* os) {
  SampleBundleBuilder builder(samples);
  std::vector<zx_info_handle_extended> infos;
  zx_status_t status;

  for (const TaskTree::Task& process : processes) {
    status =
        os->GetChildren<zx_info_handle_extended>(process.handle, process.koid,
                                                 ZX_INFO_HANDLE_TABLE,
                                                 "ZX_INFO_HANDLE_TABLE",
                                                 infos);
    if (status != ZX_OK) {
      continue;
    }

    for (const zx_info_handle_extended& info : infos) {
      if (info.type == ZX_OBJ_TYPE_CHANNEL) {
        // TODO(fxbug.dev/54364): add channel information when dockyard supports
        // multi maps. AddKoidValue(koid, "channel", entry.koid);
        builder.AddKoidValue(info.koid, "type", dockyard::KoidType::CHANNEL);
        builder.AddKoidValue(info.koid, "process", process.koid);
        builder.AddKoidValue(info.koid, "peer", info.related_koid);
      }
    }
  }
}

void GatherChannels::Gather() {
  g_slow_data_task_tree.Gather();

  SampleBundle samples;
  AddChannels(&samples, task_tree_.Processes(), os_);
  samples.Upload(DockyardPtr());
}

}  // namespace harvester
