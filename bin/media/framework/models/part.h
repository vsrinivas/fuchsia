// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace media {

// Host for a source, sink or transform.
class Part {
 public:
  virtual ~Part() {}

  // Flushes media state.
  virtual void Flush() {}
};

}  // namespace media
