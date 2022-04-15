// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/metrics.h"

#include <map>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics::feedback {
namespace {

static const auto* const kTimedOutMetrics = new std::map<std::string, cobalt::TimedOutData>({
    // Board info
    {kHardwareBoardNameKey, cobalt::TimedOutData::kBoardInfo},
    {kHardwareBoardRevisionKey, cobalt::TimedOutData::kBoardInfo},

    // Product info
    {kHardwareProductLanguageKey, cobalt::TimedOutData::kProductInfo},
    {kHardwareProductLocaleListKey, cobalt::TimedOutData::kProductInfo},
    {kHardwareProductManufacturerKey, cobalt::TimedOutData::kProductInfo},
    {kHardwareProductModelKey, cobalt::TimedOutData::kProductInfo},
    {kHardwareProductNameKey, cobalt::TimedOutData::kProductInfo},
    {kHardwareProductRegulatoryDomainKey, cobalt::TimedOutData::kProductInfo},
    {kHardwareProductSKUKey, cobalt::TimedOutData::kProductInfo},

    // Channel
    {kSystemUpdateChannelCurrentKey, cobalt::TimedOutData::kChannel},
    {kSystemUpdateChannelTargetKey, cobalt::TimedOutData::kChannel},
});

}  // namespace

AnnotationMetrics::AnnotationMetrics(cobalt::Logger* cobalt) : cobalt_(cobalt) {}

void AnnotationMetrics::LogMetrics(const Annotations& annotations) {
  std::set<cobalt::TimedOutData> to_log;
  for (const auto& [k, v] : annotations) {
    if (v == Error::kTimeout && kTimedOutMetrics->count(k) != 0) {
      to_log.insert(kTimedOutMetrics->at(k));
    }
  }

  for (const auto metric : to_log) {
    cobalt_->LogOccurrence(metric);
  }
}

}  // namespace forensics::feedback
