// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/metrics.h"

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics::feedback_data {
namespace {

static const auto* const kTimedOutMetrics = new std::map<std::string, cobalt::TimedOutData>({
    {kAttachmentLogKernel, cobalt::TimedOutData::kKernelLog},
    {kAttachmentLogSystem, cobalt::TimedOutData::kSystemLog},
    {kAttachmentInspect, cobalt::TimedOutData::kInspect},
});

}

AttachmentMetrics::AttachmentMetrics(cobalt::Logger* cobalt) : cobalt_(cobalt) {}

void AttachmentMetrics::LogMetrics(const Attachments& annotations) {
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

}  // namespace forensics::feedback_data
