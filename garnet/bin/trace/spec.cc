// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/spec.h"

#include <memory>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/json_parser/rapidjson_validation.h"

namespace tracing {
namespace {

// Top-level schema.
const char kRootSchema[] = R"({
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "test_name": {
      "type": "string"
    },
    "app": {
      "type": "string"
    },
    "args": {
      "type": "array",
      "items": {
        "type": "string"
      }
    },
    "spawn": {
      "type": "boolean"
    },
    "environment": {
      "type": "object",
      "additionalProperties": "false",
      "properties": {
        "name": {
          "type": "string"
        }
      },
      "required": ["name"]
    },
    "categories": {
      "type": "array",
      "items": {
        "type": "string"
      }
    },
    "buffering_mode": {
      "type": "string"
    },
    "buffer_size_in_mb": {
      "type": "integer",
      "minimum": 1
    },
    "provider_specs": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "name": {
            "type": "string"
          },
          "buffer_size_in_mb": {
            "type": "integer",
            "minimum": 1
          }
        },
        "required": ["name"]
      }
    },
    "duration": {
      "type": "integer",
      "minimum": 0
    },
    "measure": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "type": {
            "type": "string"
          },
          "output_test_name": {
            "type": "string"
          },
          "split_first": {
            "type": "boolean"
          },
          "expected_sample_count": {
            "type": "integer",
            "minimum": 1
          },
          "required": ["type"]
        }
      }
    },
    "test_suite_name": {
      "type": "string"
    }
  }
})";

const char kTestNameKey[] = "test_name";
const char kAppKey[] = "app";
const char kArgsKey[] = "args";
const char kSpawnKey[] = "spawn";
const char kEnvironmentKey[] = "environment";
const char kDurationKey[] = "duration";
const char kCategoriesKey[] = "categories";
const char kBufferingModeKey[] = "buffering_mode";
const char kBufferSizeInMbKey[] = "buffer_size_in_mb";
const char kProviderSpecsKey[] = "provider_specs";
const char kNameKey[] = "name";
const char kMeasurementsKey[] = "measure";
const char kTypeKey[] = "type";
const char kOutputTestName[] = "output_test_name";
const char kSplitFirstKey[] = "split_first";
const char kExpectedSampleCountKey[] = "expected_sample_count";
const char kTestSuiteNameKey[] = "test_suite_name";
const char kMeasureDurationType[] = "duration";
const char kMeasureArgumentValueType[] = "argument_value";
const char kMeasureTimeBetweenType[] = "time_between";

// Schema for "duration" measurements.
const char kDurationSchema[] = R"({
  "type": "object",
  "properties": {
    "event_category": {
      "type": "string"
    },
    "event_name": {
      "type": "string"
    }
  },
  "required": ["event_category", "event_name"]
})";
const char kEventCategoryKey[] = "event_category";
const char kEventNameKey[] = "event_name";

// Schema for "time between" measurements.
const char kTimeBetweenSchema[] = R"({
  "type": "object",
  "properties": {
    "first_event_name": {
      "type": "string"
    },
    "first_event_category": {
      "type": "string"
    },
    "first_event_anchor": {
      "type": "string"
    },
    "second_event_name": {
      "type": "string"
    },
    "second_event_category": {
      "type": "string"
    },
    "second_event_anchor": {
      "type": "string"
    }
  },
  "required": [
    "first_event_name", "first_event_category", "second_event_name",
    "second_event_category"
  ]
})";
const char kFirstEventNameKey[] = "first_event_name";
const char kFirstEventCategoryKey[] = "first_event_category";
const char kFirstEventAnchorKey[] = "first_event_anchor";
const char kSecondEventNameKey[] = "second_event_name";
const char kSecondEventCategoryKey[] = "second_event_category";
const char kSecondEventAnchorKey[] = "second_event_anchor";
const char kAnchorBegin[] = "begin";
const char kAnchorEnd[] = "end";

// Schema for "argument value" measurements.
const char kArgumentValueSchema[] = R"({
  "type": "object",
  "properties": {
    "event_category": {
      "type": "string"
    },
    "event_name": {
      "type": "string"
    },
    "argument_name": {
      "type": "string"
    },
    "argument_unit": {
      "type": "string"
    }
  },
  "required": ["event_category", "event_name", "argument_name", "argument_unit"]
})";
const char kArgumentNameKey[] = "argument_name";
const char kArgumentUnitKey[] = "argument_unit";

bool DecodeEnvironmentSpecs(const rapidjson::Value& specs, Spec* result) {
  FXL_DCHECK(result);
  FXL_DCHECK(specs.HasMember(kNameKey));
  result->environment_name = std::make_unique<std::string>(specs[kNameKey].GetString());
  return true;
}

bool DecodeProviderSpecs(const rapidjson::Value& specs, Spec* result) {
  FXL_DCHECK(specs.IsArray());
  result->provider_specs = std::make_unique<std::vector<ProviderSpec>>();
  for (const auto& spec : specs.GetArray()) {
    FXL_DCHECK(spec.HasMember(kNameKey));
    const auto& name = spec[kNameKey].GetString();
    if (spec.HasMember(kBufferSizeInMbKey)) {
      size_t size_in_mb = spec[kBufferSizeInMbKey].GetUint();
      result->provider_specs->emplace_back(ProviderSpec{name, size_in_mb});
    }
  }
  return true;
}

bool DecodeMeasureDuration(const rapidjson::Value& value, measure::DurationSpec* result) {
  result->event.name = value[kEventNameKey].GetString();
  result->event.category = value[kEventCategoryKey].GetString();
  return true;
}

bool DecodeMeasureArgumentValue(const rapidjson::Value& value, measure::ArgumentValueSpec* result) {
  result->event.name = value[kEventNameKey].GetString();
  result->event.category = value[kEventCategoryKey].GetString();
  result->argument_name = value[kArgumentNameKey].GetString();
  result->argument_unit = value[kArgumentUnitKey].GetString();
  return true;
}

bool DecodeAnchor(std::string anchor_str, const char* key, measure::Anchor* result) {
  if (anchor_str == kAnchorBegin) {
    *result = measure::Anchor::Begin;
  } else if (anchor_str == kAnchorEnd) {
    *result = measure::Anchor::End;
  } else {
    FXL_LOG(ERROR) << "Incorrect value of " << key;
    return false;
  }

  return true;
}

bool DecodeMeasureTimeBetween(const rapidjson::Value& value, measure::TimeBetweenSpec* result) {
  result->first_event.name = value[kFirstEventNameKey].GetString();
  result->first_event.category = value[kFirstEventCategoryKey].GetString();
  if (value.HasMember(kFirstEventAnchorKey)) {
    if (!DecodeAnchor(value[kFirstEventAnchorKey].GetString(), kFirstEventAnchorKey,
                      &result->first_anchor)) {
      return false;
    }
  }
  result->second_event.name = value[kSecondEventNameKey].GetString();
  result->second_event.category = value[kSecondEventCategoryKey].GetString();
  if (value.HasMember(kSecondEventAnchorKey)) {
    if (!DecodeAnchor(value[kSecondEventAnchorKey].GetString(), kSecondEventAnchorKey,
                      &result->second_anchor)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool DecodeSpec(const std::string& json, Spec* spec) {
  // Initialize schemas for JSON validation.
  auto root_schema = json_parser::InitSchema(kRootSchema);
  auto duration_schema = json_parser::InitSchema(kDurationSchema);
  auto time_between_schema = json_parser::InitSchema(kTimeBetweenSchema);
  auto argument_value_schema = json_parser::InitSchema(kArgumentValueSchema);
  if (!root_schema || !duration_schema || !time_between_schema || !argument_value_schema) {
    return false;
  }

  Spec result;
  rapidjson::Document document;
  document.Parse<rapidjson::kParseCommentsFlag>(json.c_str(), json.size());
  if (document.HasParseError()) {
    auto offset = document.GetErrorOffset();
    auto code = document.GetParseError();
    FXL_LOG(ERROR) << "Couldn't parse the tracing spec file: offset " << offset << ", "
                   << GetParseError_En(code);
    return false;
  }
  if (!json_parser::ValidateSchema(document, *root_schema)) {
    return false;
  }

  if (document.HasMember(kTestNameKey)) {
    result.test_name = std::make_unique<std::string>(document[kTestNameKey].GetString());
  }

  if (document.HasMember(kAppKey)) {
    result.app = std::make_unique<std::string>(document[kAppKey].GetString());
  }

  if (document.HasMember(kArgsKey)) {
    result.args = std::make_unique<std::vector<std::string>>();
    for (auto& arg_value : document[kArgsKey].GetArray()) {
      result.args->push_back(arg_value.GetString());
    }
  }

  if (document.HasMember(kSpawnKey)) {
    result.spawn = std::make_unique<bool>(document[kSpawnKey].GetBool());
  }

  if (document.HasMember(kEnvironmentKey)) {
    if (!DecodeEnvironmentSpecs(document[kEnvironmentKey], &result)) {
      return false;
    }
  }

  if (document.HasMember(kCategoriesKey)) {
    result.categories = std::make_unique<std::vector<std::string>>();
    for (auto& arg_value : document[kCategoriesKey].GetArray()) {
      result.categories->push_back(arg_value.GetString());
    }
  }

  if (document.HasMember(kBufferingModeKey)) {
    result.buffering_mode = std::make_unique<std::string>(document[kBufferingModeKey].GetString());
  }

  if (document.HasMember(kBufferSizeInMbKey)) {
    result.buffer_size_in_mb = std::make_unique<size_t>(document[kBufferSizeInMbKey].GetUint());
  }

  if (document.HasMember(kProviderSpecsKey)) {
    if (!DecodeProviderSpecs(document[kProviderSpecsKey], &result)) {
      return false;
    }
  }

  if (document.HasMember(kDurationKey)) {
    result.duration = std::make_unique<zx::duration>(
        zx::sec(document[kDurationKey].GetUint()));
  }

  if (document.HasMember(kTestSuiteNameKey)) {
    result.test_suite_name = std::make_unique<std::string>(document[kTestSuiteNameKey].GetString());
  }

  if (!document.HasMember(kMeasurementsKey)) {
    *spec = std::move(result);
    return true;
  }
  result.measurements = std::make_unique<measure::Measurements>();

  // Used to assign a unique id to each measurement, in the order they were
  // defined.
  uint64_t counter = 0u;
  for (auto& measurement : document[kMeasurementsKey].GetArray()) {
    std::string type = measurement[kTypeKey].GetString();
    measure::MeasurementSpecCommon common;
    common.id = counter;

    if (measurement.HasMember(kOutputTestName)) {
      common.output_test_name = measurement[kOutputTestName].GetString();
    }

    if (measurement.HasMember(kSplitFirstKey)) {
      common.split_first = measurement[kSplitFirstKey].GetBool();
    }

    if (measurement.HasMember(kExpectedSampleCountKey)) {
      common.expected_sample_count = measurement[kExpectedSampleCountKey].GetUint();
    }

    if (type == kMeasureDurationType) {
      measure::DurationSpec spec;
      spec.common = std::move(common);
      if (!json_parser::ValidateSchema(measurement, *duration_schema) ||
          !DecodeMeasureDuration(measurement, &spec)) {
        return false;
      }
      result.measurements->duration.push_back(std::move(spec));
    } else if (type == kMeasureTimeBetweenType) {
      measure::TimeBetweenSpec spec;
      spec.common = std::move(common);
      if (!json_parser::ValidateSchema(measurement, *time_between_schema) ||
          !DecodeMeasureTimeBetween(measurement, &spec)) {
        return false;
      }
      result.measurements->time_between.push_back(std::move(spec));
    } else if (type == kMeasureArgumentValueType) {
      measure::ArgumentValueSpec spec;
      spec.common = std::move(common);
      if (!json_parser::ValidateSchema(measurement, *argument_value_schema) ||
          !DecodeMeasureArgumentValue(measurement, &spec)) {
        return false;
      }
      result.measurements->argument_value.push_back(std::move(spec));
    } else {
      FXL_LOG(ERROR) << "Unrecognized measurement type: " << type;
      return false;
    }

    counter++;
  }
  *spec = std::move(result);
  return true;
}

const BufferingModeSpec kBufferingModes[] = {{"oneshot", BufferingMode::kOneshot},
                                             {"circular", BufferingMode::kCircular},
                                             {"streaming", BufferingMode::kStreaming}};

const BufferingModeSpec* LookupBufferingMode(const std::string& name) {
  for (const auto& mode : kBufferingModes) {
    if (name == mode.name) {
      return &mode;
    }
  }
  return nullptr;
}

}  // namespace tracing
