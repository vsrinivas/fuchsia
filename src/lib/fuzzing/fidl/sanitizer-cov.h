// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_SANITIZER_COV_H_
#define SRC_LIB_FUZZING_FIDL_SANITIZER_COV_H_

#define SANITIZER_COV_HEADER_ONLY

// Generates the headers for the __sanitizer_cov_* interface.
#include "sanitizer-cov.inc"

#undef SANITIZER_COV_HEADER_ONLY

#endif  // SRC_LIB_FUZZING_FIDL_SANITIZER_COV_H_
