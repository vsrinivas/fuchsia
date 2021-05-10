// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ERRORS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ERRORS_H_

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace crash_reports {

// Provide a reason why |error| occurred that is specific to feedback data collection.
inline std::string ToReason(const Error error) {
  switch (error) {
    case Error::kLogicError:
      return "feedback logic error";
    case Error::kTimeout:
      return "timeout";
    case Error::kConnectionError:
      return "FIDL connection error";
    case Error::kAsyncTaskPostFailure:
      return "async post task failure";
    case Error::kMissingValue:
      return "missing";
    case Error::kBadValue:
      return "bad data returned";
    case Error::kFileReadFailure:
      return "file read failure";
    case Error::kFileWriteFailure:
      return "file write failure";
    case Error::kCustom:
      FX_LOGS(FATAL) << "Error::kCustom does not have a reason";
      return "FATAL, THIS SHOULD NOT HAPPEN";
    case Error::kDefault:
      FX_LOGS(FATAL) << "Error::kDefault does not have a reason";
      return "FATAL, THIS SHOULD NOT HAPPEN";
    case Error::kNotSet:
      FX_LOGS(FATAL) << "Error::kNotSet does not have a reason";
      return "FATAL, THIS SHOULD NOT HAPPEN";
  }
}

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ERRORS_H_
