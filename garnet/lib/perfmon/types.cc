// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/strings/string_printf.h>

#include "types.h"

namespace perfmon {

std::string ReaderStatusToString(ReaderStatus status) {
  switch (status) {
    case ReaderStatus::kOk:
      return "Ok";
    case ReaderStatus::kNoMoreRecords:
      return "NoMoreRecords";
    case ReaderStatus::kHeaderError:
      return "HeaderError";
    case ReaderStatus::kRecordError:
      return "RecordError";
    case ReaderStatus::kIoError:
      return "IoError";
    case ReaderStatus::kInvalidArgs:
      return "InvalidArgs";
    default:
      return fxl::StringPrintf("UNKNOWN:%u", static_cast<unsigned>(status));
  }
}

}  // namespace perfmon
