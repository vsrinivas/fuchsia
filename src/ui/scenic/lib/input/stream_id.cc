// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/stream_id.h"

namespace scenic_impl::input {

StreamId NewStreamId() {
  static StreamId next_id = 1;
  return next_id++;
}

}  // namespace scenic_impl::input
