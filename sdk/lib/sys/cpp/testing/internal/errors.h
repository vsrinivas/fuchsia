// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/realm/builder/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#ifndef LIB_SYS_CPP_TESTING_INTERNAL_ERRORS_H_
#define LIB_SYS_CPP_TESTING_INTERNAL_ERRORS_H_

namespace sys::testing::internal {

const char* ConvertToString(fuchsia::realm::builder::RealmBuilderError& error);
const char* ConvertToString(fuchsia::component::Error& error);
void PanicWithMessage(const char* stacktrace, const char* context, zx_status_t status);
void PanicWithMessage(const char* stacktrace, const char* context,
                      fuchsia::realm::builder::RealmBuilderError& error);
void PanicWithMessage(const char* stacktrace, const char* context,
                      fuchsia::component::Error& error);

}  // namespace sys::testing::internal

#define ASSERT_STATUS_OK(method, status)                                                   \
  do {                                                                                     \
    if ((status) != ZX_OK) {                                                               \
      ::sys::testing::internal::PanicWithMessage(__PRETTY_FUNCTION__, (method), (status)); \
    }                                                                                      \
  } while (0)

#define ASSERT_RESULT_OK(method, result)                                                         \
  do {                                                                                           \
    if ((result).is_err()) {                                                                     \
      ::sys::testing::internal::PanicWithMessage(__PRETTY_FUNCTION__, (method), (result).err()); \
    }                                                                                            \
  } while (0)

#define ASSERT_STATUS_AND_RESULT_OK(method, status, result) \
  do {                                                      \
    ASSERT_STATUS_OK((method), (status));                   \
    ASSERT_RESULT_OK((method), (result));                   \
  } while (0)

#endif  // LIB_SYS_CPP_TESTING_INTERNAL_ERRORS_H_
