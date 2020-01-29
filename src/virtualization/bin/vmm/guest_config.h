// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_GUEST_CONFIG_H_
#define SRC_VIRTUALIZATION_BIN_VMM_GUEST_CONFIG_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace guest_config {

void PrintCommandLineUsage(const char* program_name);
// Parses command-line arguments.
zx_status_t ParseArguments(int argc, const char** argv, fuchsia::virtualization::GuestConfig* cfg);

// Parses json configuration.
zx_status_t ParseConfig(const std::string& data, fuchsia::virtualization::GuestConfig* cfg);

void SetDefaults(fuchsia::virtualization::GuestConfig* cfg);

}  // namespace guest_config

#endif  // SRC_VIRTUALIZATION_BIN_VMM_GUEST_CONFIG_H_
