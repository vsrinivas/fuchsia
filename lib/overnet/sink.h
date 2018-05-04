// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "callback.h"
#include "status.h"

namespace overnet {

// A typed data sink for some kind of object
template <class T>
class Sink {
 public:
  virtual ~Sink() {}
  virtual void Close(const Status& status) = 0;
  virtual void Push(const T& item, StatusCallback done) = 0;
};

}  // namespace overnet
