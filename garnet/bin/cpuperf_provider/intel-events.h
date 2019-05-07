// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_INTEL_EVENTS_H_
#define GARNET_BIN_CPUPERF_PROVIDER_INTEL_EVENTS_H_

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>

namespace cpuperf_provider {

enum EventId {

#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, \
                        readable_name, description) \
  symbol = perfmon::MakeEventId(perfmon::kGroupFixed, id),
#define DEF_ARCH_EVENT(symbol, event_name, id, ebx_bit, event, \
                       umask, flags, readable_name, description) \
  symbol = perfmon::MakeEventId(perfmon::kGroupArch, id),
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>

#define DEF_SKL_EVENT(symbol, event_name, id, event, umask, \
                      flags, readable_name, description) \
  symbol = perfmon::MakeEventId(perfmon::kGroupModel, id),
#include <lib/zircon-internal/device/cpu-trace/skylake-pm-events.inc>

#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, \
                           flags, readable_name, description) \
  symbol = perfmon::MakeEventId(perfmon::kGroupMisc, id),
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>

};

}  // namespace cpuperf_provider

#endif // GARNET_BIN_CPUPERF_PROVIDER_INTEL_EVENTS_H_
