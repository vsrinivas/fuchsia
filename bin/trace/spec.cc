// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/trace/spec.h"

#include "lib/ftl/logging.h"

#include <rapidjson/document.h>

namespace tracing {
namespace {

const char kAppKey[] = "app";
const char kArgsKey[] = "args";
const char kDurationKey[] = "duration";
const char kCategoriesKey[] = "categories";
const char kMeasurementsKey[] = "measure";

const char kTypeKey[] = "type";
const char kEventNameKey[] = "event_name";
const char kEventCategoryKey[] = "event_category";
const char kFirstEventNameKey[] = "first_event_name";
const char kFirstEventCategoryKey[] = "first_event_category";
const char kFirstEventAnchorKey[] = "first_event_anchor";
const char kSecondEventNameKey[] = "second_event_name";
const char kSecondEventCategoryKey[] = "second_event_category";
const char kSecondEventAnchorKey[] = "second_event_anchor";

const char kMeasureDurationType[] = "duration";
const char kMeasureTimeBetweenType[] = "time_between";
const char kAnchorBegin[] = "begin";
const char kAnchorEnd[] = "end";

bool DecodeStringMember(const rapidjson::Value& value,
                        const char* key,
                        std::string* result) {
  FTL_DCHECK(value.IsObject());
  if (!value.HasMember(key) || !value[key].IsString()) {
    FTL_LOG(ERROR) << "Missing or not a string: " << key;
    return false;
  }

  *result = value[key].GetString();
  return true;
}

bool DecodeAnchorMember(const rapidjson::Value& value,
                        const char* key,
                        measure::Anchor* result) {
  std::string anchor_str;
  if (!DecodeStringMember(value, key, &anchor_str)) {
    return false;
  }

  if (anchor_str == kAnchorBegin) {
    *result = measure::Anchor::Begin;
  } else if (anchor_str == kAnchorEnd) {
    *result = measure::Anchor::End;
  } else {
    FTL_LOG(ERROR) << "Incorrect value of " << key;
    return false;
  }

  return true;
}

bool DecodeMeasureDurationSpec(const rapidjson::Value& value,
                               measure::DurationSpec* result) {
  FTL_DCHECK(value.IsObject());
  if (!DecodeStringMember(value, kEventNameKey, &result->event.name)) {
    return false;
  }
  if (!DecodeStringMember(value, kEventCategoryKey, &result->event.category)) {
    return false;
  }
  return true;
}

bool DecodeMeasureTimeBetweenSpec(const rapidjson::Value& value,
                                  measure::TimeBetweenSpec* result) {
  FTL_DCHECK(value.IsObject());
  if (!DecodeStringMember(value, kFirstEventNameKey,
                          &result->first_event.name)) {
    return false;
  }
  if (!DecodeStringMember(value, kFirstEventCategoryKey,
                          &result->first_event.category)) {
    return false;
  }
  if (value.HasMember(kFirstEventAnchorKey)) {
    if (!DecodeAnchorMember(value, kFirstEventAnchorKey,
                            &result->first_anchor)) {
      return false;
    }
  }
  if (!DecodeStringMember(value, kSecondEventNameKey,
                          &result->second_event.name)) {
    return false;
  }
  if (!DecodeStringMember(value, kSecondEventCategoryKey,
                          &result->second_event.category)) {
    return false;
  }
  if (value.HasMember(kSecondEventAnchorKey)) {
    if (!DecodeAnchorMember(value, kSecondEventAnchorKey,
                            &result->second_anchor)) {
      return false;
    }
  }
  return true;
}

bool DecodeStringArrayMember(const rapidjson::Value& value,
                             const char* key,
                             std::vector<std::string>* result) {
  FTL_DCHECK(value.IsObject());
  if (!value.HasMember(key)) {
    return true;
  }

  if (!value[key].IsArray()) {
    FTL_LOG(ERROR) << "Incorrect format of " << key << " - not an array";
    return false;
  }

  for (auto& arg_value : value[key].GetArray()) {
    if (!arg_value.IsString()) {
      FTL_LOG(ERROR) << "Incorrect value within " << key << " - not a string";
      return false;
    }
    result->push_back(arg_value.GetString());
  }
  return true;
}

}  // namespace

bool DecodeSpec(const std::string& json, Spec* spec) {
  Spec result;

  rapidjson::Document document;
  document.Parse(json.c_str(), json.size());
  if (document.HasParseError() || !document.IsObject()) {
    FTL_LOG(ERROR) << "Couldn't parse the benchmark config.";
    return false;
  }

  if (document.HasMember(kAppKey)) {
    if (!document[kAppKey].IsString()) {
      FTL_LOG(ERROR) << "Incorrect format of " << kAppKey << " - not a string";
      return false;
    }
    result.app = document[kAppKey].GetString();
  }

  if (!DecodeStringArrayMember(document, kArgsKey, &result.args)) {
    return false;
  }

  if (!DecodeStringArrayMember(document, kCategoriesKey, &result.categories)) {
    return false;
  }

  if (document.HasMember(kDurationKey)) {
    if (!document[kDurationKey].IsUint()) {
      FTL_LOG(ERROR) << "Incorrect format of " << kDurationKey
                     << " - not an uint";
      return false;
    }
    result.duration =
        ftl::TimeDelta::FromSeconds(document[kDurationKey].GetUint());
  }

  if (!document.HasMember(kMeasurementsKey)) {
    *spec = result;
    return true;
  }

  // Used to assign a unique id to each measurement, in the order they were
  // defined.
  uint64_t counter = 0u;
  if (!document[kMeasurementsKey].IsArray()) {
    FTL_LOG(ERROR) << "Incorrect format of " << kMeasurementsKey
                   << " - not an array";
    return false;
  }
  for (auto& measurement : document[kMeasurementsKey].GetArray()) {
    if (!measurement.HasMember(kTypeKey) || !measurement[kTypeKey].IsString()) {
      FTL_LOG(ERROR) << "Missing or incorrect measurement type";
      return false;
    }
    std::string type = measurement[kTypeKey].GetString();

    if (type == kMeasureDurationType) {
      measure::DurationSpec spec;
      spec.id = counter;
      if (!DecodeMeasureDurationSpec(measurement, &spec)) {
        return false;
      }
      result.duration_specs.push_back(std::move(spec));
      counter++;
    } else if (type == kMeasureTimeBetweenType) {
      measure::TimeBetweenSpec spec;
      spec.id = counter;
      if (!DecodeMeasureTimeBetweenSpec(measurement, &spec)) {
        return false;
      }
      result.time_between_specs.push_back(std::move(spec));
      counter++;
    } else {
      FTL_LOG(ERROR) << "Unrecognized measurement type: " << type;
      return false;
    }
  }
  *spec = result;
  return true;
}
}  // namespace tracing
