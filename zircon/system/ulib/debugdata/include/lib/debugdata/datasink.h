// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DEBUGDATA_DATASINK_H_
#define LIB_DEBUGDATA_DATASINK_H_

#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include <cinttypes>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fbl/unique_fd.h>

namespace debugdata {

using DataSinkCallback = fit::function<void(std::string)>;

/// Represents a single dumpfile element.
struct DumpFile {
  std::string name;  // Name of the dumpfile.
  std::string file;  // File name for the content.
};

/// Processes debug data and returns all files written to `data_sink_dir_fd` and mapped by
/// data_sink. This function will process all data sinks and execute callbacks with error or
/// warnings.
std::unordered_map<std::string, std::vector<DumpFile>> ProcessDebugData(
    const fbl::unique_fd& data_sink_dir_fd,
    std::unordered_map<std::string, std::vector<zx::vmo>> debug_data,
    DataSinkCallback error_callback, DataSinkCallback warning_callback);

}  // namespace debugdata

#endif  // LIB_DEBUGDATA_DATASINK_H_
