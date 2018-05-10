// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_LOGGING_H_
#define LIB_FIDL_CPP_INTERNAL_LOGGING_H_

#include <lib/fidl/cpp/message.h>

namespace fidl {
namespace internal {

void ReportEncodingError(const Message& message, const fidl_type_t* type,
                         const char* error_msg, const char* file, int line);

void ReportDecodingError(const Message& message, const fidl_type_t* type,
                         const char* error_msg, const char* file, int line);

void ReportChannelWritingError(const Message& message, const fidl_type_t* type,
                               zx_status_t status, const char* file, int line);

#define FIDL_REPORT_ENCODING_ERROR(message, type, error_msg)            \
  ::fidl::internal::ReportEncodingError((message), (type), (error_msg), \
                                        __FILE__, __LINE__)

#define FIDL_REPORT_DECODING_ERROR(message, type, error_msg)            \
  ::fidl::internal::ReportDecodingError((message), (type), (error_msg), \
                                        __FILE__, __LINE__)

#define FIDL_REPORT_CHANNEL_WRITING_ERROR(message, type, status)           \
  ::fidl::internal::ReportChannelWritingError((message), (type), (status), \
                                              __FILE__, __LINE__)

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_LOGGING_H_
