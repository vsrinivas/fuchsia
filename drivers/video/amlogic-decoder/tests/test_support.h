// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_SUPPORT_H_
#define TEST_SUPPORT_H_

#include <ddk/device.h>

#include <zircon/errors.h>
#include <zircon/syscalls.h>

class TestSupport {
 public:
  static zx_device_t* parent_device();

  static void set_parent_device(zx_device_t* handle);

  static void RunAllTests();
};

#endif  // TEST_SUPPORT_H_
