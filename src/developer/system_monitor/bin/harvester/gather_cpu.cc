// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_cpu.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "harvester.h"
#include "sample_bundle.h"

namespace harvester {

void GatherCpu::GatherDeviceProperties() {
  const std::string CPU_COUNT = "cpu:count";
  zx_info_cpu_stats_t stats[1];
  size_t actual, avail;
  zx_status_t err = zx_object_get_info(InfoResource(), ZX_INFO_CPU_STATS,
                                       &stats, sizeof(stats), &actual, &avail);
  if (err != ZX_OK) {
    FX_LOGS(ERROR) << ZxErrorString("ZX_INFO_CPU_STATS", err);
    return;
  }
  SampleList list;
  list.emplace_back(CPU_COUNT, avail);
  DockyardProxyStatus status = Dockyard().SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FX_LOGS(ERROR) << DockyardErrorString("SendSampleList", status)
                   << " The cpu_count value will be missing";
  }
}

void GatherCpu::Gather() {}

}  // namespace harvester
