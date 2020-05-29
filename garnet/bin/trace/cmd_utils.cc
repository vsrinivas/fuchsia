// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/cmd_utils.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <iostream>

#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace tracing {

bool ParseBufferingMode(const std::string& value, BufferingMode* out_mode) {
  const BufferingModeSpec* spec = LookupBufferingMode(value);
  if (spec == nullptr) {
    FX_LOGS(ERROR) << "Failed to parse buffering mode: " << value;
    return false;
  }
  *out_mode = spec->mode;
  return true;
}

static bool CheckBufferSize(uint32_t megabytes) {
  if (megabytes < kMinBufferSizeMegabytes || megabytes > kMaxBufferSizeMegabytes) {
    FX_LOGS(ERROR) << "Buffer size not between " << kMinBufferSizeMegabytes << ","
                   << kMaxBufferSizeMegabytes << ": " << megabytes;
    return false;
  }
  return true;
}

bool ParseBufferSize(const std::string& value, uint32_t* out_buffer_size) {
  uint32_t megabytes;
  if (!fxl::StringToNumberWithError(value, &megabytes)) {
    FX_LOGS(ERROR) << "Failed to parse buffer size: " << value;
    return false;
  }
  if (!CheckBufferSize(megabytes)) {
    return false;
  }
  *out_buffer_size = megabytes;
  return true;
}

bool ParseProviderBufferSize(const std::vector<std::string_view>& values,
                             std::vector<ProviderSpec>* out_specs) {
  for (const auto& value : values) {
    size_t colon = value.rfind(':');
    if (colon == value.npos) {
      FX_LOGS(ERROR) << "Syntax error in provider buffer size"
                     << ": should be provider-name:buffer_size_in_mb";
      return false;
    }
    uint32_t megabytes;
    if (!fxl::StringToNumberWithError(value.substr(colon + 1), &megabytes)) {
      FX_LOGS(ERROR) << "Failed to parse buffer size: " << value;
      return false;
    }
    if (!CheckBufferSize(megabytes)) {
      return false;
    }
    // We can't verify the provider name here, all we can do is pass it on.
    std::string name = std::string(value.substr(0, colon));
    out_specs->emplace_back(ProviderSpec{name, megabytes});
  }
  return true;
}

bool ParseTriggers(const std::vector<std::string_view>& values,
                   std::unordered_map<std::string, Action>* out_specs) {
  FX_DCHECK(out_specs);

  for (const auto& value : values) {
    size_t colon = value.rfind(':');
    if (colon == value.npos || colon < 1 || colon > value.size() - 2) {
      FX_LOGS(ERROR) << "Syntax error in trigger specification: "
                     << "should be alert-name:action, got " << value;
      return false;
    }
    std::string name = std::string(value.substr(0, colon));
    if (out_specs->find(name) != out_specs->end()) {
      FX_LOGS(ERROR) << "Multiple trigger options for alert: " << name;
      return false;
    }
    Action action;
    if (!ParseAction(value.substr(colon + 1), &action)) {
      FX_LOGS(ERROR) << "Unrecognized action: " << value.substr(colon + 1);
      return false;
    }
    out_specs->emplace(name, action);
  }
  return true;
}

bool ParseAction(std::string_view value, Action* out_action) {
  FX_DCHECK(out_action);

  if (value == kActionStop) {
    *out_action = Action::kStop;
    return true;
  }
  return false;
}

controller::BufferingMode TranslateBufferingMode(BufferingMode mode) {
  switch (mode) {
    case BufferingMode::kOneshot:
      return controller::BufferingMode::ONESHOT;
    case BufferingMode::kCircular:
      return controller::BufferingMode::CIRCULAR;
    case BufferingMode::kStreaming:
      return controller::BufferingMode::STREAMING;
    default:
      FX_NOTREACHED();
      return controller::BufferingMode::ONESHOT;
  }
}

std::vector<controller::ProviderSpec> TranslateProviderSpecs(
    const std::vector<ProviderSpec>& specs) {
  // Uniquify the list, with later entries overriding earlier entries.
  std::map<std::string, uint32_t> spec_map;
  for (const auto& it : specs) {
    spec_map[it.name] = it.buffer_size_in_mb;
  }
  std::vector<controller::ProviderSpec> uniquified_specs;
  for (const auto& it : spec_map) {
    controller::ProviderSpec spec;
    spec.set_name(it.first);
    spec.set_buffer_size_megabytes_hint(it.second);
    uniquified_specs.push_back(std::move(spec));
  }
  return uniquified_specs;
}

const char* StartErrorCodeToString(controller::StartErrorCode code) {
  switch (code) {
    case controller::StartErrorCode::NOT_INITIALIZED:
      return "not initialized";
    case controller::StartErrorCode::ALREADY_STARTED:
      return "already started";
    case controller::StartErrorCode::STOPPING:
      return "stopping";
    case controller::StartErrorCode::TERMINATING:
      return "terminating";
    default:
      return "<unknown>";
  }
}

}  // namespace tracing
