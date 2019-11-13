// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/transform_handle.h"

namespace std {

ostream& operator<<(ostream& out, const flatland::TransformHandle& h) {
  out << "(" << h.graph_id_ << ":" << h.transform_id_ << ")";
  return out;
}

}  // namespace std
