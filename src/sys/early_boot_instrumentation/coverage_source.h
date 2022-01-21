// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_EARLY_BOOT_INSTRUMENTATION_COVERAGE_SOURCE_H_
#define SRC_SYS_EARLY_BOOT_INSTRUMENTATION_COVERAGE_SOURCE_H_

#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <string_view>

#include <fbl/unique_fd.h>
#include <sdk/lib/vfs/cpp/pseudo_dir.h>

namespace early_boot_instrumentation {

// Filenames for each exposed profraw/symbolizer files.

// Zircon's profraw and optionally symbolizer log.
static constexpr std::string_view kKernelFile = "zircon.profraw";

// This file may be available, only if, the kernel exposes a symbolizer log as well.
// This might eventually be replaced by self-describing profraw file.
static constexpr std::string_view kKernelSymbolizerFile = "zircon.log";

// Given a handle to |kernel_data_dir|, will extract the kernel coverage vmos from it,
// and add them as VMO file into |out_dir|.
//
// Usually |kernel_data_dir| is '/boot/kernel/data'.
zx::status<> ExposeKernelProfileData(fbl::unique_fd& kernel_data_dir, vfs::PseudoDir& out_dir);

}  // namespace early_boot_instrumentation

#endif  // SRC_SYS_EARLY_BOOT_INSTRUMENTATION_COVERAGE_SOURCE_H_
