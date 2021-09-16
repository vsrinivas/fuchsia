// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_ZSTD_INCLUDE_ZSTD_ZSTD_ERRORS_H_
#define THIRD_PARTY_ZSTD_INCLUDE_ZSTD_ZSTD_ERRORS_H_

// TODO(fxbug.dev/83607): temporary during soft transition
#ifdef _TEMPORARY_ZSTD_148
#include "../../src/lib/common/zstd_errors.h"
#else
#include "../../src/lib/zstd_errors.h"
#endif

#endif  // THIRD_PARTY_ZSTD_INCLUDE_ZSTD_ZSTD_ERRORS_H_
