// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FXL_ARRAYSIZE_H_
#define SRC_LIB_FXL_ARRAYSIZE_H_

#include <iterator>

#ifndef arraysize
#define arraysize(array) ::std::size(array)
#endif

#endif  // SRC_LIB_FXL_ARRAYSIZE_H_
