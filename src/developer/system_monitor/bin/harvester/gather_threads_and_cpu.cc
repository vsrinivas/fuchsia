// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_threads_and_cpu.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "gather_cpu.h"
#include "gather_tasks.h"
#include "sample_bundle.h"
#include "task_tree.h"

namespace harvester {

GatherThreadsAndCpu::GatherThreadsAndCpu(
    zx_handle_t info_resource, harvester::DockyardProxy* dockyard_proxy)
    : GatherCategory(info_resource, dockyard_proxy) {}

void GatherThreadsAndCpu::Gather() {
  SampleBundle samples;
  auto task_tree = &g_fast_data_task_tree;
  if (actions_.WantRefresh()) {
    task_tree->Gather();
    AddTaskBasics(&samples, task_tree->Jobs(), dockyard::KoidType::JOB);
    AddTaskBasics(&samples, task_tree->Processes(),
                  dockyard::KoidType::PROCESS);
    AddTaskBasics(&samples, task_tree->Threads(), dockyard::KoidType::THREAD);
  }
  AddThreadStats(&samples, task_tree->Threads());
  AddGlobalCpuSamples(&samples, InfoResource());
  samples.Upload(DockyardPtr());
  actions_.NextInterval();
}

}  // namespace harvester
