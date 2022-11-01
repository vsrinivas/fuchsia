// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace component_testing {
namespace internal {

const char* ConvertToString(fuchsia::component::test::RealmBuilderError& error) {
  switch (error) {
    case fuchsia::component::test::RealmBuilderError::CHILD_ALREADY_EXISTS:
      return "CHILD_ALREADY_EXISTS";
    case fuchsia::component::test::RealmBuilderError::INVALID_MANIFEST_EXTENSION:
      return "INVALID_MANIFEST_EXTENSION";
    case fuchsia::component::test::RealmBuilderError::INVALID_COMPONENT_DECL:
      return "INVALID_COMPONENT_DECL";
    case fuchsia::component::test::RealmBuilderError::NO_SUCH_CHILD:
      return "NO_SUCH_CHILD";
    case fuchsia::component::test::RealmBuilderError::CHILD_DECL_NOT_VISIBLE:
      return "CHILD_DECL_NOT_VISIBLE";
    case fuchsia::component::test::RealmBuilderError::NO_SUCH_SOURCE:
      return "NO_SUCH_SOURCE";
    case fuchsia::component::test::RealmBuilderError::NO_SUCH_TARGET:
      return "NO_SUCH_TARGET";
    case fuchsia::component::test::RealmBuilderError::CAPABILITIES_EMPTY:
      return "CAPABILITIES_EMPTY";
    case fuchsia::component::test::RealmBuilderError::TARGETS_EMPTY:
      return "TARGETS_EMPTY";
    case fuchsia::component::test::RealmBuilderError::SOURCE_AND_TARGET_MATCH:
      return "SOURCE_AND_TARGET_MATCH";
    case fuchsia::component::test::RealmBuilderError::DECL_NOT_FOUND:
      return "DECL_NOT_FOUND";
    case fuchsia::component::test::RealmBuilderError::DECL_READ_ERROR:
      return "DECL_READ_ERROR";
    case fuchsia::component::test::RealmBuilderError::BUILD_ALREADY_CALLED:
      return "BUILD_ALREADY_CALLED";
    case fuchsia::component::test::RealmBuilderError::CAPABILITY_INVALID:
      return "CAPABILITY_INVALID";
    case fuchsia::component::test::RealmBuilderError::INVALID_CHILD_REALM_HANDLE:
      return "INVALID_CHILD_REALM_HANDLE";
    default:
      return "UNKNOWN";
  }
}

const char* ConvertToString(fuchsia::component::Error& error) {
  switch (error) {
    case fuchsia::component::Error::INTERNAL:
      return "INTERNAL";
    case fuchsia::component::Error::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case fuchsia::component::Error::UNSUPPORTED:
      return "UNSUPPORTED";
    case fuchsia::component::Error::ACCESS_DENIED:
      return "ACCESS_DENIED";
    case fuchsia::component::Error::INSTANCE_NOT_FOUND:
      return "INSTANCE_NOT_FOUND";
    case fuchsia::component::Error::INSTANCE_ALREADY_EXISTS:
      return "INSTANCE_ALREADY_EXISTS";
    case fuchsia::component::Error::INSTANCE_CANNOT_START:
      return "INSTANCE_CANNOT_START";
    case fuchsia::component::Error::INSTANCE_CANNOT_RESOLVE:
      return "INSTANCE_CANNOT_RESOLVE";
    case fuchsia::component::Error::COLLECTION_NOT_FOUND:
      return "COLLECTION_NOT_FOUND";
    case fuchsia::component::Error::RESOURCE_UNAVAILABLE:
      return "RESOURCE_UNAVAILABLE";
    case fuchsia::component::Error::INSTANCE_DIED:
      return "INSTANCE_DIED";
    default:
      return "UNKNOWN";
  }
}

void PanicWithMessage(const char* stacktrace, const char* context, zx_status_t status) {
  ZX_PANIC("[%s] FIDL method %s failed with status: %s", stacktrace, context,
           zx_status_get_string(status));
}

void PanicWithMessage(const char* stacktrace, const char* context,
                      fuchsia::component::test::RealmBuilderError& error) {
  ZX_PANIC("[%s] FIDL method %s failed with error: %s", stacktrace, context,
           ConvertToString(error));
}

void PanicWithMessage(const char* stacktrace, const char* context,
                      fuchsia::component::Error& error) {
  ZX_PANIC("[%s] FIDL method %s failed with error: %s", stacktrace, context,
           ConvertToString(error));
}

}  // namespace internal
}  // namespace component_testing
