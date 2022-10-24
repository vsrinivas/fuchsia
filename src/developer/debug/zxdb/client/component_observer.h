// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_COMPONENT_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_COMPONENT_OBSERVER_H_

#include <stddef.h>

#include <string>

namespace zxdb {

class ComponentObserver {
 public:
  // Called when a component has started.
  virtual void OnComponentStarted(const std::string& moniker, const std::string& url) {}

  // Called when a component has exited.
  virtual void OnComponentExited(const std::string& moniker, const std::string& url) {}

  // Called when a test component has exited and the test_runner has been fully cleaned up.
  virtual void OnTestExited(const std::string& url) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_COMPONENT_OBSERVER_H_
