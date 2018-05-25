// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

class Err;
class Process;
class Thread;

class ProcessObserver {
 public:
  // Called immediately after creating a new thread and before destroying it.
  virtual void DidCreateThread(Process* process, Thread* thread) {}
  virtual void WillDestroyThread(Process* process, Thread* thread) {}

  // Called when symbols for a loaded binary could not be loaded.
  virtual void OnSymbolLoadFailure(Process* process, const Err& err) {}
};

}  // namespace zxdb
