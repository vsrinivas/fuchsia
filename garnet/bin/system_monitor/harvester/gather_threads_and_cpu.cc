// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_threads_and_cpu.h"

#include <string>

#include "gather_cpu.h"
#include "gather_tasks.h"
#include "sample_bundle.h"
#include "src/lib/fxl/logging.h"
#include "task_tree.h"

namespace harvester {

GatherThreadsAndCpu::GatherThreadsAndCpu(
    zx_handle_t root_resource, harvester::DockyardProxy* dockyard_proxy)
    : GatherCategory(root_resource, dockyard_proxy), task_tree_(new TaskTree) {}

GatherThreadsAndCpu::~GatherThreadsAndCpu() { delete task_tree_; }

void GatherThreadsAndCpu::Gather() {
  SampleBundle samples;
  if (actions_.WantRefresh()) {
    task_tree_->Gather();
    AddTaskBasics(&samples, task_tree_->Jobs(), dockyard::KoidType::JOB);
    AddTaskBasics(&samples, task_tree_->Processes(),
                  dockyard::KoidType::PROCESS);
    AddTaskBasics(&samples, task_tree_->Threads(), dockyard::KoidType::THREAD);
  }
  AddThreadStats(&samples, task_tree_->Threads());
  AddGlobalCpuSamples(&samples, RootResource());
  samples.Upload(DockyardPtr());
  actions_.NextInterval();
}

}  // namespace harvester
