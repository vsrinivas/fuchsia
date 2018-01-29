// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_TEST_LOOP_CONFIG_H_
#define LIB_FIDL_CPP_TEST_LOOP_CONFIG_H_

#include <async/cpp/loop.h>

namespace fidl {

extern const async_loop_config_t kTestLoopConfig;

}  // namespace fidl

#endif  // LIB_FIDL_CPP_TEST_LOOP_CONFIG_H_
