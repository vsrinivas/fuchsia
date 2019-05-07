// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_ARM64_EVENTS_H_
#define GARNET_BIN_CPUPERF_PROVIDER_ARM64_EVENTS_H_

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>

namespace cpuperf_provider {

enum EventId {

#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, \
                        readable_name, description) \
  symbol = perfmon::MakeEventId(perfmon::kGroupFixed, id),
#define DEF_ARCH_EVENT(symbol, event_name, id, pmceid_bit, event, \
                       flags, readable_name, description) \
  symbol = perfmon::MakeEventId(perfmon::kGroupArch, id),
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>

};

}  // namespace cpuperf_provider

#endif // GARNET_BIN_CPUPERF_PROVIDER_ARM64_EVENTS_H_
