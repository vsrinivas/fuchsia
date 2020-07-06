// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_CMD_UTILS_H_
#define GARNET_BIN_TRACE_CMD_UTILS_H_

#include <fuchsia/tracing/controller/cpp/fidl.h>
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "garnet/bin/trace/options.h"

namespace tracing {

namespace controller = ::fuchsia::tracing::controller;

constexpr uint32_t kDefaultDurationSeconds = 10;
constexpr uint32_t kDefaultBufferSizeMegabytes = 4;
constexpr controller::BufferingMode kDefaultBufferingMode = controller::BufferingMode::ONESHOT;

constexpr char kDefaultOutputFileName[] = "/tmp/trace.json";
constexpr char kDefaultBinaryOutputFileName[] = "/tmp/trace.fxt";

constexpr char kActionStop[] = "stop";

bool ParseBufferingMode(const std::string& value, BufferingMode* out_mode);

bool ParseBufferSize(const std::string& value, uint32_t* out_buffer_size);

bool ParseProviderBufferSize(const std::vector<std::string_view>& values,
                             std::vector<ProviderSpec>* out_specs);

bool ParseTriggers(const std::vector<std::string_view>& values,
                   std::unordered_map<std::string, Action>* out_specs);

bool ParseAction(std::string_view value, Action* out_action);

controller::BufferingMode TranslateBufferingMode(BufferingMode mode);

// Uniquify the list, with later entries overriding earlier entries,
// and convert to the FIDL form.
std::vector<controller::ProviderSpec> TranslateProviderSpecs(
    const std::vector<ProviderSpec>& specs);

const char* StartErrorCodeToString(controller::StartErrorCode code);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_CMD_UTILS_H_
