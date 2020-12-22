// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_processes_and_memory.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "gather_memory.h"
#include "gather_tasks.h"
#include "sample_bundle.h"
#include "task_tree.h"

namespace harvester {

GatherProcessesAndMemory::GatherProcessesAndMemory(
    zx_handle_t info_resource, harvester::DockyardProxy* dockyard_proxy)
    : GatherCategory(info_resource, dockyard_proxy) {}

void GatherProcessesAndMemory::Gather() {
  SampleBundle samples;
  // Note: g_slow_data_task_tree is gathered in GatherChannels::Gather().
  auto task_tree = &g_slow_data_task_tree;
  if (actions_.WantRefresh()) {
    AddTaskBasics(&samples, task_tree->Jobs(), dockyard::KoidType::JOB);
    AddTaskBasics(&samples, task_tree->Processes(),
                  dockyard::KoidType::PROCESS);
    AddTaskBasics(&samples, task_tree->Threads(), dockyard::KoidType::THREAD);
  }
  AddProcessStats(&samples, task_tree->Processes());
  AddGlobalMemorySamples(&samples, InfoResource());
  samples.Upload(DockyardPtr());
  actions_.NextInterval();
}

}  // namespace harvester
