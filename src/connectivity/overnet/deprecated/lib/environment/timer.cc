// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/environment/timer.h"

namespace overnet {

void Timer::At(TimeStamp t, StatusCallback f) {
  struct CB {
    CB(StatusCallback f) : fn(std::move(f)) {}
    Optional<Timeout> timeout;
    StatusCallback fn;
    bool done = false;
    bool initialized = false;
  };
  auto* cb = new CB(std::move(f));
  cb->timeout.Reset(this, t, [cb](const Status& status) {
    cb->fn(status);
    cb->done = true;
    if (cb->done && cb->initialized) {
      delete cb;
    }
  });
  cb->initialized = true;
  if (cb->done && cb->initialized) {
    delete cb;
  }
}

}  // namespace overnet
