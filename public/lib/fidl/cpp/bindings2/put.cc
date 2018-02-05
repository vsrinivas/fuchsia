// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/put.h"

namespace fidl {

bool PutAt(Builder* builder, zx_handle_t* view, zx::object_base* object) {
  *view = object->release();
  return true;
}

}  // namespace fidl
