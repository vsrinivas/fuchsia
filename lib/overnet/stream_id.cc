// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_id.h"
#include <sstream>

namespace overnet {

std::string StreamId::ToString() const {
  std::ostringstream tmp;
  tmp << id_;
  return tmp.str();
}

std::ostream& operator<<(std::ostream& out, StreamId stream_id) {
  return out << stream_id.ToString();
}

}  // namespace overnet
