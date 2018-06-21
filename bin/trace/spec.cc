// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/spec.h"

#include <memory>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#include "lib/fxl/logging.h"

namespace tracing {
namespace {

// Top-level schema.
const char kRootSchema[] = R"({
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "app": {
      "type": "string"
    },
    "args": {
      "type": "array",
      "items": {
        "type": "string"
      }
    },
    "categories": {
      "type": "array",
      "items": {
        "type": "string"
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
          "split_samples_at": {
            "type": "array",
            "items": {
              "type": "integer",
              "minimum": 0
            }
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
const char kAppKey[] = "app";
const char kArgsKey[] = "args";
const char kDurationKey[] = "duration";
const char kCategoriesKey[] = "categories";
const char kMeasurementsKey[] = "measure";
const char kTypeKey[] = "type";
const char kSplitSamplesAtKey[] = "split_samples_at";
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

bool DecodeMeasureDuration(const rapidjson::Value& value,
                           measure::DurationSpec* result) {
  result->event.name = value[kEventNameKey].GetString();
  result->event.category = value[kEventCategoryKey].GetString();
  return true;
}

bool DecodeMeasureArgumentValue(const rapidjson::Value& value,
                                measure::ArgumentValueSpec* result) {
  result->event.name = value[kEventNameKey].GetString();
  result->event.category = value[kEventCategoryKey].GetString();
  result->argument_name = value[kArgumentNameKey].GetString();
  result->argument_unit = value[kArgumentUnitKey].GetString();
  return true;
}

bool DecodeAnchor(std::string anchor_str, const char* key,
                  measure::Anchor* result) {
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

bool DecodeMeasureTimeBetween(const rapidjson::Value& value,
                              measure::TimeBetweenSpec* result) {
  result->first_event.name = value[kFirstEventNameKey].GetString();
  result->first_event.category = value[kFirstEventCategoryKey].GetString();
  if (value.HasMember(kFirstEventAnchorKey)) {
    if (!DecodeAnchor(value[kFirstEventAnchorKey].GetString(),
                      kFirstEventAnchorKey, &result->first_anchor)) {
      return false;
    }
  }
  result->second_event.name = value[kSecondEventNameKey].GetString();
  result->second_event.category = value[kSecondEventCategoryKey].GetString();
  if (value.HasMember(kSecondEventAnchorKey)) {
    if (!DecodeAnchor(value[kSecondEventAnchorKey].GetString(),
                      kSecondEventAnchorKey, &result->second_anchor)) {
      return false;
    }
  }
  return true;
}

std::unique_ptr<rapidjson::SchemaDocument> InitSchema(const char schemaSpec[]) {
  rapidjson::Document schema_document;
  if (schema_document.Parse(schemaSpec).HasParseError()) {
    FXL_DCHECK(false) << "Schema validation spec itself is not valid JSON.";
    return nullptr;
  }
  return std::make_unique<rapidjson::SchemaDocument>(schema_document);
}

bool ValidateSchema(const rapidjson::Value& value,
                    const rapidjson::SchemaDocument& schema) {
  rapidjson::SchemaValidator validator(schema);
  if (!value.Accept(validator)) {
    rapidjson::StringBuffer uri_buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(uri_buffer);
    FXL_LOG(ERROR) << "Incorrect schema of tracing spec at "
                   << uri_buffer.GetString() << " , schema violation: "
                   << validator.GetInvalidSchemaKeyword();
    return false;
  }
  return true;
}

}  // namespace

bool DecodeSpec(const std::string& json, Spec* spec) {
  // Initialize schemas for JSON validation.
  auto root_schema = InitSchema(kRootSchema);
  auto duration_schema = InitSchema(kDurationSchema);
  auto time_between_schema = InitSchema(kTimeBetweenSchema);
  auto argument_value_schema = InitSchema(kArgumentValueSchema);
  if (!root_schema || !duration_schema || !time_between_schema ||
      !argument_value_schema) {
    return false;
  }

  Spec result;
  rapidjson::Document document;
  document.Parse(json.c_str(), json.size());
  if (document.HasParseError()) {
    auto offset = document.GetErrorOffset();
    auto code = document.GetParseError();
    FXL_LOG(ERROR) << "Couldn't parse the tracing spec file: offset " << offset
                   << ", " << GetParseError_En(code);
    return false;
  }
  if (!ValidateSchema(document, *root_schema)) {
    return false;
  }

  if (document.HasMember(kAppKey)) {
    result.app = document[kAppKey].GetString();
  }

  if (document.HasMember(kArgsKey)) {
    for (auto& arg_value : document[kArgsKey].GetArray()) {
      result.args.push_back(arg_value.GetString());
    }
  }
  if (document.HasMember(kCategoriesKey)) {
    for (auto& arg_value : document[kCategoriesKey].GetArray()) {
      result.categories.push_back(arg_value.GetString());
    }
  }

  if (document.HasMember(kDurationKey)) {
    result.duration =
        fxl::TimeDelta::FromSeconds(document[kDurationKey].GetUint());
  }

  if (document.HasMember(kTestSuiteNameKey)) {
    result.test_suite_name = document[kTestSuiteNameKey].GetString();
  }

  if (!document.HasMember(kMeasurementsKey)) {
    *spec = result;
    return true;
  }

  // Used to assign a unique id to each measurement, in the order they were
  // defined.
  uint64_t counter = 0u;
  for (auto& measurement : document[kMeasurementsKey].GetArray()) {
    std::string type = measurement[kTypeKey].GetString();

    if (type == kMeasureDurationType) {
      measure::DurationSpec spec;
      spec.id = counter;
      if (!ValidateSchema(measurement, *duration_schema) ||
          !DecodeMeasureDuration(measurement, &spec)) {
        return false;
      }
      result.measurements.duration.push_back(std::move(spec));
    } else if (type == kMeasureTimeBetweenType) {
      measure::TimeBetweenSpec spec;
      spec.id = counter;
      if (!ValidateSchema(measurement, *time_between_schema) ||
          !DecodeMeasureTimeBetween(measurement, &spec)) {
        return false;
      }
      result.measurements.time_between.push_back(std::move(spec));
    } else if (type == kMeasureArgumentValueType) {
      measure::ArgumentValueSpec spec;
      spec.id = counter;
      if (!ValidateSchema(measurement, *argument_value_schema) ||
          !DecodeMeasureArgumentValue(measurement, &spec)) {
        return false;
      }
      result.measurements.argument_value.push_back(std::move(spec));
    } else {
      FXL_LOG(ERROR) << "Unrecognized measurement type: " << type;
      return false;
    }

    if (measurement.HasMember(kSplitSamplesAtKey)) {
      for (auto& value : measurement[kSplitSamplesAtKey].GetArray()) {
        if (!result.measurements.split_samples_at[counter].empty() &&
            value.GetUint() <=
                result.measurements.split_samples_at[counter].back()) {
          FXL_LOG(ERROR)
              << "Incorrect split samples at values - not strictly increasing.";
          return false;
        }
        result.measurements.split_samples_at[counter].push_back(
            value.GetUint());
      }
    }

    if (measurement.HasMember(kExpectedSampleCountKey)) {
      result.measurements.expected_sample_count[counter] =
          measurement[kExpectedSampleCountKey].GetUint();
    }

    counter++;
  }
  *spec = result;
  return true;
}
}  // namespace tracing
