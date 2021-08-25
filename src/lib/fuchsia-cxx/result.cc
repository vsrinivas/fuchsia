// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fuchsia-cxx/result.h"

namespace rust::zx {

Result<> make_result(ffi::Result result) {
  if (result.status == ZX_OK) {
    return ok();
  }
  return error{result};
}

}  // namespace rust::zx
