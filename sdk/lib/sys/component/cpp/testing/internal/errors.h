// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_ERRORS_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_ERRORS_H_

namespace component_testing {
namespace internal {

const char* ConvertToString(fuchsia::component::test::RealmBuilderError2& error);
const char* ConvertToString(fuchsia::component::Error& error);
void PanicWithMessage(const char* stacktrace, const char* context, zx_status_t status);
void PanicWithMessage(const char* stacktrace, const char* context,
                      fuchsia::component::test::RealmBuilderError2& error);
void PanicWithMessage(const char* stacktrace, const char* context,
                      fuchsia::component::Error& error);

}  // namespace internal
}  // namespace component_testing

#define ZX_COMPONENT_ASSERT_STATUS_OK(method, status)                                           \
  do {                                                                                          \
    if ((status) != ZX_OK) {                                                                    \
      ::component_testing::internal::PanicWithMessage(__PRETTY_FUNCTION__, (method), (status)); \
    }                                                                                           \
  } while (0)

#define ZX_COMPONENT_ASSERT_RESULT_OK(method, result)                                \
  do {                                                                               \
    if ((result).is_err()) {                                                         \
      ::component_testing::internal::PanicWithMessage(__PRETTY_FUNCTION__, (method), \
                                                      (result).err());               \
    }                                                                                \
  } while (0)

#define ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(method, status, result) \
  do {                                                                   \
    ZX_COMPONENT_ASSERT_STATUS_OK((method), (status));                   \
    ZX_COMPONENT_ASSERT_RESULT_OK((method), (result));                   \
  } while (0)

#define ZX_SYS_ASSERT_NOT_NULL(value)                                                           \
  do {                                                                                          \
    ZX_ASSERT_MSG((value) != nullptr, "[%s] %s must not be null", __PRETTY_FUNCTION__, #value); \
  } while (0)

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_ERRORS_H_
