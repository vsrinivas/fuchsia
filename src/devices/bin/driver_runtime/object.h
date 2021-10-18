// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_OBJECT_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_OBJECT_H_

#include <fbl/ref_counted.h>

class Object : public fbl::RefCounted<Object> {
 public:
  virtual ~Object() = default;
};

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_OBJECT_H_
