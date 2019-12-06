// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_EXECUTION_SCOPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_EXECUTION_SCOPE_H_

#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Target;
class Thread;

// Some things like breakpoints might be tied to a "scope" where they apply to. For example, a
// breakpoing could apply globally, to one target, or to one thread.
class ExecutionScope {
 public:
  enum Type {
    kSystem,  // Global.
    kTarget,  // Applies to all threads of a target.
    kThread   // Applies to only one thread.
  };

  ExecutionScope() = default;          // System (global) scope.
  explicit ExecutionScope(Target* t);  // Target scope.
  explicit ExecutionScope(Thread* t);  // Thread scope.

  Type type() const { return type_; }

  // Possibly null! See variables below for requirements.
  Target* target() const { return target_.get(); }
  Thread* thread() const { return thread_.get(); }

 private:
  Type type_ = kSystem;

  // The target or thread may get deleted before this class does so any pointers can be null
  // even if the type() matches.
  fxl::WeakPtr<Target> target_;  // Possibly valid when type_ == kTarget or type_ == KThread;
  fxl::WeakPtr<Thread> thread_;  // Possibly valid when type_ == kThread;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_EXECUTION_SCOPE_H_
