// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_COMPONENT_CONTAINER_H_
#define SRC_SYS_APPMGR_COMPONENT_CONTAINER_H_

namespace component {

template <typename T>
class ComponentContainer {
 public:
  virtual std::shared_ptr<T> ExtractComponent(T* controller) = 0;
  virtual ~ComponentContainer() {}
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_COMPONENT_CONTAINER_H_
