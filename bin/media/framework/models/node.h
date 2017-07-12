// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace media {

// A source, sink or transform in a graph.
class Node {
 public:
  virtual ~Node() {}

  // Flushes media state.
  virtual void Flush() {}
};

}  // namespace media
