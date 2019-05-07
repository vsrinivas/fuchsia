// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_CONFIG_IMPL_H_
#define GARNET_LIB_PERFMON_CONFIG_IMPL_H_

#include "garnet/lib/perfmon/config.h"

namespace perfmon {
namespace internal {

// Convert the config to what the ioctl requires.
Config::Status PerfmonToIoctlConfig(const Config& config,
                                    perfmon_ioctl_config_t* out_config);

}  // namespace internal
}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_CONFIG_IMPL_H_
