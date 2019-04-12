// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_COMPONENT_CONTAINER_H_
#define GARNET_BIN_APPMGR_COMPONENT_CONTAINER_H_

template <typename T>
class ComponentContainer {
 public:
  virtual std::shared_ptr<T> ExtractComponent(T* controller) = 0;
  virtual ~ComponentContainer(){};
};

#endif  // GARNET_BIN_APPMGR_COMPONENT_CONTAINER_H_
