// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CPUPERF_TYPES_H_
#define GARNET_LIB_CPUPERF_TYPES_H_

#include <string>

namespace cpuperf {

enum class ReaderStatus {
  kOk,
  kNoMoreRecords,
  kHeaderError,
  kRecordError,
  kIoError,
  kInvalidArgs,
};

std::string ReaderStatusToString(ReaderStatus status);

}  // namespace cpuperf

#endif  // GARNET_LIB_CPUPERF_TYPES_H_
