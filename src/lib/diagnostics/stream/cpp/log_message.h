// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DIAGNOSTICS_STREAM_CPP_LOG_MESSAGE_H_
#define SRC_LIB_DIAGNOSTICS_STREAM_CPP_LOG_MESSAGE_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fit/result.h>

#include <vector>

namespace diagnostics::stream {

fit::result<std::vector<fit::result<fuchsia::logger::LogMessage, std::string>>, std::string>
ConvertFormattedContentToLogMessages(fuchsia::diagnostics::FormattedContent content);

}  // namespace diagnostics::stream

#endif  // SRC_LIB_DIAGNOSTICS_STREAM_CPP_LOG_MESSAGE_H_
