// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_TEST_HANDLE_UTIL_H_
#define LIB_FIDL_CPP_TEST_HANDLE_UTIL_H_

#include <lib/zx/handle.h>

namespace fidl {
namespace test {
namespace util {

zx_handle_t create_channel();

zx_handle_t create_event();

}  // namespace util
}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_TEST_HANDLE_UTIL_H_
