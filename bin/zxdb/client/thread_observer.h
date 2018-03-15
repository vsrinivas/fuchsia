// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

class Thread;

// Note: thread lifetime notifications are on the ProcessObserver.
class ThreadObserver {
 public:
  virtual void OnThreadStopped(Thread* thread) {}
};

}  // namespace zxdb
