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

#endif  // THIRD_PARTY_PIGWEED_BACKENDS_PW_LOG_DFV1_PUBLIC_OVERRIDES_PW_LOG_BACKEND_LOG_BACKEND_H_
