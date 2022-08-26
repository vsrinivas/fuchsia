// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_PIGWEED_BACKENDS_PW_LOG_DFV1_PUBLIC_OVERRIDES_PW_LOG_BACKEND_LOG_BACKEND_H_
#define THIRD_PARTY_PIGWEED_BACKENDS_PW_LOG_DFV1_PUBLIC_OVERRIDES_PW_LOG_BACKEND_LOG_BACKEND_H_

#include "pw_preprocessor/arguments.h"
#include "pw_preprocessor/compiler.h"
#include "pw_preprocessor/util.h"

PW_EXTERN_C_START

void pw_Log(int level, unsigned int flags, const char* file_name, int line_number,
            const char* message, ...) PW_PRINTF_FORMAT(5, 6);

PW_EXTERN_C_END

#define PW_HANDLE_LOG(level, flags, message, ...) \
  pw_Log((level), (flags), __FILE__, __LINE__, message PW_COMMA_ARGS(__VA_ARGS__))

// Use printf for logging. The first 2 bits of the PW_HANDLE_LOG "flags" int are reserved, so use
// the third bit.
#define PW_LOG_FLAG_USE_PRINTF 1 << 2
// When specified, the log message should not be logged. This is useful for disabling log levels at
// runtime.
#define PW_LOG_FLAG_IGNORE 1 << 3

namespace pw_log_ddk {

// Returns the part of a path following the final '/', or the whole path if there is no '/'.
constexpr const char* BaseName(const char* path) {
  for (const char* c = path; c && (*c != '\0'); c++) {
    if (*c == '/') {
      path = c + 1;
    }
  }
  return path;
}

}  // namespace pw_log_ddk

#endif  // THIRD_PARTY_PIGWEED_BACKENDS_PW_LOG_DFV1_PUBLIC_OVERRIDES_PW_LOG_BACKEND_LOG_BACKEND_H_
