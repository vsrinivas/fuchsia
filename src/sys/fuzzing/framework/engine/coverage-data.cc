// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/coverage-data.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

uint64_t GetTargetId(const zx::process& process) {
  zx_info_handle_basic_t info;
  if (auto status = process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
      status != ZX_OK) {
    FX_LOGS(WARNING) << " Failed to get target id for process: " << zx_status_get_string(status);
    return kInvalidTargetId;
  }
  return info.koid;
}

uint64_t GetTargetId(const std::string& data_sink) {
  auto target_id_str = data_sink.substr(0, data_sink.find('/'));
  char* endptr = nullptr;
  auto target_id = strtoull(target_id_str.c_str(), &endptr, 0);
  if (*endptr != '\0') {
    FX_LOGS(WARNING) << "failed to parse target id: invalid character: '" << *endptr << "'";
    return kInvalidTargetId;
  }
  return target_id;
}

std::string GetModuleId(const std::string& data_sink) {
  auto pos = data_sink.find('/');
  return pos == std::string::npos ? "" : data_sink.substr(pos + 1);
}

}  // namespace fuzzing
