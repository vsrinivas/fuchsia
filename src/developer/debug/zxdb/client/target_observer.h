// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_OBSERVER_H_

namespace zxdb {

class Target;

class TargetObserver {
 public:
  virtual void DidCreateTarget(Target* target) {}
  virtual void WillDestroyTarget(Target* target) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_OBSERVER_H_
