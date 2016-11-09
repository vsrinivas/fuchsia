// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DART_SDK_EXT_SRC_NATIVES_H_
#define LIB_FIDL_DART_SDK_EXT_SRC_NATIVES_H_

#include "dart/runtime/include/dart_api.h"

namespace fidl {
namespace dart {

Dart_NativeFunction NativeLookup(Dart_Handle name,
                                 int argument_count,
                                 bool* auto_setup_scope);

const uint8_t* NativeSymbol(Dart_NativeFunction nf);

}  // namespace dart
}  // namespace fidl

#endif  // LIB_FIDL_DART_SDK_EXT_SRC_NATIVES_H_
