// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf/session_spec.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

#include <array>
#include <limits>
#include <string>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#include "garnet/lib/perfmon/events.h"
#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/json_parser/rapidjson_validation.h"

namespace cpuperf {

namespace {

// Top-level schema.
const char kRootSchema[] = R"({
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "config_name": {
      "type": "string"
    },
    "model_name": {
      "type": "string"
    },
    "events": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "group_name": {
            "type": "string"
          },
          "event_name": {
            "type": "string"
          },
          "rate": {
            "type": "integer"
          },
          "flags": {
            "type": "array",
            "uniqueItems": true,
            "items": {
              "type": "string",
              "enum": [
                "os",
                "user",
                "pc",
                "last_branch",
                "timebase"
              ]
            }
          },
          "required": [ "group_name", "event_name" ]
        }
      }
    },
    "buffer_size_in_mb": {
      "type": "integer",
      "minimum": 1
    },
    "duration": {
      "type": "integer",
      "minimum": 0
    },
    "num_iterations": {
      "type": "integer",
      "minimum": 1
    },
    "output_path_prefix": {
      "type": "string"
    },
    "session_result_spec_path": {
      "type": "string"
    },
    "required": [ "events" ]
  }
})";

const char kConfigNameKey[] = "config_name";
const char kModelNameKey[] = "model_name";
const char kEventsKey[] = "events";
const char kGroupNameKey[] = "group_name";
const char kEventNameKey[] = "event_name";
const char kRateKey[] = "rate";
const char kFlagsKey[] = "flags";
const char kDurationKey[] = "duration";
const char kBufferSizeInMbKey[] = "buffer_size_in_mb";
const char kNumIterationsKey[] = "num_iterations";
const char kOutputPathPrefixKey[] = "output_path_prefix";
const char kSessionResultSpecPathKey[] = "session_result_spec_path";

template <typename T>
bool DecodeEvents(T events, const perfmon::ModelEventManager* model_event_manager,
                  SessionSpec* out_spec) {
  FXL_VLOG(1) << "Processing " << events.Size() << " events";

  for (const auto& event : events) {
    perfmon::EventId id = perfmon::kEventIdNone;
    perfmon::EventRate rate = 0;
    uint32_t flags = 0;
    if (!event.HasMember(kGroupNameKey) || !event.HasMember(kEventNameKey)) {
      FXL_LOG(ERROR) << "Event is missing group_name,event_name fields";
      return false;
    }
    const std::string& group_name = event[kGroupNameKey].GetString();
    const std::string& event_name = event[kEventNameKey].GetString();
    const perfmon::EventDetails* details;
    if (!model_event_manager->LookupEventByName(group_name.c_str(), event_name.c_str(), &details)) {
      FXL_LOG(ERROR) << "Unknown event: " << group_name << ":" << event_name;
      return false;
    }
    id = details->id;
    if (event.HasMember(kRateKey)) {
      rate = event[kRateKey].GetUint();
    }
    if (event.HasMember(kFlagsKey)) {
      for (const auto& flag : event[kFlagsKey].GetArray()) {
        if (!flag.IsString()) {
          FXL_LOG(ERROR) << "Flag for event " << group_name << ":" << event_name
                         << " is not a string";
          return false;
        }
        const std::string& flag_name = flag.GetString();
        if (flag_name == "os") {
          flags |= perfmon::Config::kFlagOs;
        } else if (flag_name == "user") {
          flags |= perfmon::Config::kFlagUser;
        } else if (flag_name == "pc") {
          flags |= perfmon::Config::kFlagPc;
        } else if (flag_name == "timebase") {
          flags |= perfmon::Config::kFlagTimebase;
        } else if (flag_name == "last_branch") {
          flags |= perfmon::Config::kFlagLastBranch;
        } else {
          FXL_LOG(ERROR) << "Unknown flag for event " << group_name << ":" << event_name << ": "
                         << flag_name;
          return false;
        }
      }
    }

    FXL_VLOG(2) << "Found event: " << group_name << ":" << event_name << ", id 0x" << std::hex << id
                << ", rate " << std::dec << rate << ", flags 0x" << std::hex << flags;

    perfmon::Config::Status status = out_spec->perfmon_config.AddEvent(id, rate, flags);
    if (status != perfmon::Config::Status::OK) {
      FXL_LOG(ERROR) << "Error processing event configuration: "
                     << perfmon::Config::StatusToString(status);
      return false;
    }
  }

  return true;
}

}  // namespace

bool DecodeSessionSpec(const std::string& json, SessionSpec* out_spec) {
  // Initialize schemas for JSON validation.
  auto root_schema = json_parser::InitSchema(kRootSchema);
  if (!root_schema) {
    return false;
  }

  SessionSpec result;
  rapidjson::Document document;
  document.Parse<rapidjson::kParseCommentsFlag>(json.c_str(), json.size());
  if (document.HasParseError()) {
    auto offset = document.GetErrorOffset();
    auto code = document.GetParseError();
    FXL_LOG(ERROR) << "Couldn't parse the session config file: offset " << offset << ", "
                   << GetParseError_En(code);
    return false;
  }
  if (!json_parser::ValidateSchema(document, *root_schema, "session config")) {
    return false;
  }

  if (document.HasMember(kConfigNameKey)) {
    result.config_name = document[kConfigNameKey].GetString();
  }

  if (document.HasMember(kModelNameKey)) {
    result.model_name = document[kModelNameKey].GetString();
  }
  if (result.model_name == SessionSpec::kDefaultModelName) {
    result.model_name = perfmon::GetDefaultModelName();
  }

  std::unique_ptr<perfmon::ModelEventManager> model_event_manager =
      perfmon::ModelEventManager::Create(result.model_name);
  if (!model_event_manager) {
    FXL_LOG(ERROR) << "Unsupported model: " << result.model_name;
    return false;
  }

  if (document.HasMember(kEventsKey)) {
    const auto& events = document[kEventsKey].GetArray();
    if (events.Size() == 0) {
      FXL_LOG(ERROR) << "Need at least one event";
      return false;
    }
    if (!DecodeEvents(events, model_event_manager.get(), &result)) {
      return false;
    }
  }

  if (document.HasMember(kBufferSizeInMbKey)) {
    result.buffer_size_in_mb = document[kBufferSizeInMbKey].GetUint();
  }

  if (document.HasMember(kDurationKey)) {
    result.duration = zx::sec(document[kDurationKey].GetUint());
  }

  if (document.HasMember(kNumIterationsKey)) {
    result.num_iterations = document[kNumIterationsKey].GetUint();
  }

  if (document.HasMember(kOutputPathPrefixKey)) {
    result.output_path_prefix = document[kOutputPathPrefixKey].GetString();
  }

  if (document.HasMember(kSessionResultSpecPathKey)) {
    result.session_result_spec_path = document[kSessionResultSpecPathKey].GetString();
  }

  *out_spec = std::move(result);
  out_spec->model_event_manager = std::move(model_event_manager);
  return true;
}

const char SessionSpec::kDefaultModelName[] = "default";
const char SessionSpec::kDefaultOutputPathPrefix[] = "/tmp/cpuperf";
const char SessionSpec::kDefaultSessionResultSpecPath[] = "/tmp/cpuperf.cpsession";

SessionSpec::SessionSpec()
    : model_name(kDefaultModelName),
      output_path_prefix(kDefaultOutputPathPrefix),
      session_result_spec_path(kDefaultSessionResultSpecPath) {}

}  // namespace cpuperf
