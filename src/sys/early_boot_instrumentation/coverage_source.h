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

// Physboot's profraw and optionally symbolizer log.
static constexpr std::string_view kPhysFile = "physboot.profraw";

// This file may be available, only if, the physboot exposes a symbolizer log as well.
// This might eventually be replaced by self-describing profraw file.
static constexpr std::string_view kPhysSymbolizerFile = "physboot.log";

// Subdirectory names for each type of debugdata.
static constexpr std::string_view kDynamicDir = "dynamic";
static constexpr std::string_view kStaticDir = "static";

// llvm-profile sink and extension.
static constexpr std::string_view kLlvmSink = "llvm-profile";
static constexpr std::string_view kLlvmSinkExtension = "profraw";

// Alias for str to unique_ptr<PseudoDir> map that allows lookup by string_view.
using SinkDirMap = std::map<std::string, std::unique_ptr<vfs::PseudoDir>, std::less<>>;

// Given a handle to |kernel_data_dir|, will extract the kernel coverage vmos from it,
// and add them as VMO file into |sink_map| as if it where published with the sink "llvm-profile".
//
// Usually |kernel_data_dir| is '/boot/kernel/data'.
zx::result<> ExposeKernelProfileData(fbl::unique_fd& kernel_data_dir, SinkDirMap& sink_map);

// Given a handle to |phys_data_dir|, will extract the physboot's coverage vmos from it,
// and add them as VMO file into |sink_map| as if it where published with the sink "llvm-profile".
//
// Usually |phys_data_dir| is '/boot/kernel/data/phys'.
zx::result<> ExposePhysbootProfileData(fbl::unique_fd& physboot_data_dir, SinkDirMap& sink_map);

// Given a channel speaking the |fuchsia.boot.SvcStash| protocol, this will extract all published
// debug data, and return a map from 'sink_name' to a root directory for each sink. Each root
// directory contains two child directories, 'static' and 'dynamic'.
//
// Following the |debugdata.Publisher/Publish| protocol, data associated to a publish request is
// considered 'static' if the provided token(|zx::eventpair|) in the request has the
// |ZX_EVENTPAIR_PEER_CLOSED| signal. Otherwise, it's considered 'dynamic'.
//
// Once the data associated with a request has been tagged as 'static' or 'dynamic' it is exposed
// as a |vfs::VmoFile| under the respective root directory of the |sink_name| associated with the
// request.
//
// The filenames are generated as follow:
//    Each stashed handle is assigned an index (monotonically increasing) 'svc_id'.
//    Each request in the stashed handle is assigned another index (monotonically increasing)
//    Each published vmo has a names 'vmo_name'.
//    'req_id'. Then the name generated for the data associated with the request(svc_id, req_id) =
//    "svc_id"-"req_id"."vmo_name".
// In essence "vmo_name" acts like the extension.
SinkDirMap ExtractDebugData(zx::unowned_channel svc_stash);

}  // namespace early_boot_instrumentation

#endif  // SRC_SYS_EARLY_BOOT_INSTRUMENTATION_COVERAGE_SOURCE_H_
