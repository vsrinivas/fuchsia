// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CODING_H_
#define LIB_FIDL_LLCPP_CODING_H_

#include <zircon/fidl.h>

namespace fidl {

// The table of any FIDL method with zero in/out parameters.
extern "C" const fidl_type_t _llcpp_coding_AnyZeroArgMessageTable;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CODING_H_
