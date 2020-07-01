// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <ostream>

#ifndef SRC_LIB_FIDL_CODEC_LOGGER_H_
#define SRC_LIB_FIDL_CODEC_LOGGER_H_

namespace fidl_codec::logger {

namespace internal {

extern thread_local std::ostream* log_stream_tls;

}  // namespace internal

// For capturing logs to a given stream.  Use the macro FX_LOGS_OR_CAPTURE when logging, and
// read the result by using LogCapturer:
// ostringstream ss;
// LogCapturer capturer(&ss);
// FX_LOGS_OR_CAPTURE(ERROR) << "Foo";
// ASSERT_EQ(ss.str(), "Foo");  // should be true.
class LogCapturer {
 public:
  LogCapturer(std::ostream* stream) {
    old_stream_ = internal::log_stream_tls;
    internal::log_stream_tls = stream;
  }
  ~LogCapturer() { internal::log_stream_tls = old_stream_; }
  std::ostream& stream() { return *internal::log_stream_tls; }

 private:
  std::ostream* old_stream_;
};

}  // namespace fidl_codec::logger

#define FX_LOGS_OR_CAPTURE(severity)                                                           \
  !FX_LOG_IS_ON(severity) ? true                                                               \
                          : true && ((fidl_codec::logger::internal::log_stream_tls != nullptr) \
                                         ? (*fidl_codec::logger::internal::log_stream_tls)     \
                                         : FX_LOG_STREAM(severity, nullptr))

#endif  // SRC_LIB_FIDL_CODEC_LOGGER_H_
