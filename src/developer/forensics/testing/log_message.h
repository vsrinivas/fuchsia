// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_LOG_MESSAGE_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_LOG_MESSAGE_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

namespace forensics {
namespace testing {

// Returns a LogMessage with the given severity, message and optional tags.
//
// The process and thread ids are constants. The timestamp is a constant plus the optionally
// provided offset.
fuchsia::logger::LogMessage BuildLogMessage(const int32_t severity, const std::string& text,
                                            const zx::duration timestamp_offset = zx::duration(0),
                                            const std::vector<std::string>& tags = {});

}  // namespace testing
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_LOG_MESSAGE_H_
