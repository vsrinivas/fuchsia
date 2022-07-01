// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/coverage-data.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <third_party/modp_b64/modp_b64.h>

namespace fuzzing {
namespace {

// Number of characters needed to encode a target ID. The padding character and null terminator are
// not counted.
constexpr auto kTargetIdLen = modp_b64_encode_len(sizeof(uint64_t)) - 2;

std::string GetName(const zx::vmo& vmo) {
  char name[ZX_MAX_NAME_LEN];
  if (auto status = vmo.get_property(ZX_PROP_NAME, name, sizeof(name)); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to get VMO name: " << zx_status_get_string(status);
    return std::string();
  }
  return std::string(name);
}

}  // namespace

uint64_t GetTargetId(const zx::process& process) {
  zx_info_handle_basic_t info;
  if (auto status = process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
      status != ZX_OK) {
    FX_LOGS(WARNING) << " Failed to get target id for process: " << zx_status_get_string(status);
    return kInvalidTargetId;
  }
  return info.koid;
}

uint64_t GetTargetId(const zx::vmo& inline_8bit_counters) {
  return GetTargetId(GetName(inline_8bit_counters));
}

uint64_t GetTargetId(const std::string& name) {
  if (name.size() < kTargetIdLen) {
    FX_LOGS(WARNING) << "Failed to decode target id: " << name;
    return kInvalidTargetId;
  }
  // See target/module.cc. The last character should be the omitted padding.
  auto encoded = name.substr(0, kTargetIdLen) + "=";
  uint64_t target_id;
  auto len = modp_b64_decode(reinterpret_cast<char*>(&target_id), encoded.data(), encoded.size());
  if (len == size_t(-1)) {
    FX_LOGS(WARNING) << "Failed to decode target id: " << name;
    return kInvalidTargetId;
  }
  return target_id;
}

std::string GetModuleId(const zx::vmo& inline_8bit_counters) {
  return GetModuleId(GetName(inline_8bit_counters));
}

std::string GetModuleId(const std::string& name) {
  if (name.size() < kTargetIdLen) {
    FX_LOGS(WARNING) << "Failed to get module ID from '" << name << "'";
    return std::string();
  }
  return name.substr(kTargetIdLen);
}

}  // namespace fuzzing
