// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_TRANSITION_H_
#define LIB_FIDL_CPP_TRANSITION_H_

// Define this to use fit::optional in StringPtr and VectorPtr definitions
#define FIDL_USE_FIT_OPTIONAL
// Enable deprecation warnings for methods going away after the fit::optional transition
// #define FIDL_FIT_OPTIONAL_DEPRECATION

// A macro for (optional) deprecation warnings
#if defined(FIDL_FIT_OPTIONAL_DEPRECATION)
#define FIDL_FIT_OPTIONAL_DEPRECATED(M) [[deprecated(M)]]
#else
#define FIDL_FIT_OPTIONAL_DEPRECATED(M) [[]]
#endif

#endif  // LIB_FIDL_CPP_TRANSITION_H_
