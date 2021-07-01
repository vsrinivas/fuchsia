// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/realm/builder/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace sys::testing::internal {

const char* ConvertToString(fuchsia::realm::builder::RealmBuilderError& error) {
  switch (error) {
    case fuchsia::realm::builder::RealmBuilderError::NODE_BEHIND_CHILD_DECL:
      return "NODE_BEHIND_CHILD_DECL";
    case fuchsia::realm::builder::RealmBuilderError::NO_SUCH_CHILD:
      return "NO_SUCH_CHILD";
    case fuchsia::realm::builder::RealmBuilderError::ROOT_CANNOT_BE_SET_TO_URL:
      return "ROOT_CANNOT_BE_SET_TO_URL";
    case fuchsia::realm::builder::RealmBuilderError::ROOT_CANNOT_BE_EAGER:
      return "ROOT_CANNOT_BE_EAGER";
    case fuchsia::realm::builder::RealmBuilderError::BAD_FIDL:
      return "BAD_FIDL";
    case fuchsia::realm::builder::RealmBuilderError::MISSING_FIELD:
      return "MISSING_FIELD";
    case fuchsia::realm::builder::RealmBuilderError::ROUTE_TARGETS_EMPTY:
      return "ROUTE_TARGETS_EMPTY";
    case fuchsia::realm::builder::RealmBuilderError::MISSING_ROUTE_SOURCE:
      return "MISSING_ROUTE_SOURCE";
    case fuchsia::realm::builder::RealmBuilderError::MISSING_ROUTE_TARGET:
      return "MISSING_ROUTE_TARGET";
    case fuchsia::realm::builder::RealmBuilderError::ROUTE_SOURCE_AND_TARGET_MATCH:
      return "ROUTE_SOURCE_AND_TARGET_MATCH";
    case fuchsia::realm::builder::RealmBuilderError::VALIDATION_ERROR:
      return "VALIDATION_ERROR";
    case fuchsia::realm::builder::RealmBuilderError::UNABLE_TO_EXPOSE:
      return "UNABLE_TO_EXPOSE";
    case fuchsia::realm::builder::RealmBuilderError::STORAGE_SOURCE_INVALID:
      return "STORAGE_SOURCE_INVALID";
    case fuchsia::realm::builder::RealmBuilderError::MONIKER_NOT_FOUND:
      return "MONIKER_NOT_FOUND";
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
  ZX_PANIC("[%s] FIDL method %s failed with status: %s\n", stacktrace, context,
           zx_status_get_string(status));
}

void PanicWithMessage(const char* stacktrace, const char* context,
                      fuchsia::realm::builder::RealmBuilderError& error) {
  ZX_PANIC("[%s] FIDL method %s failed with error: %s\n", stacktrace, context,
           ConvertToString(error));
}

void PanicWithMessage(const char* stacktrace, const char* context,
                      fuchsia::component::Error& error) {
  ZX_PANIC("[%s] FIDL method %s failed with error: %s\n", stacktrace, context,
           ConvertToString(error));
}

}  // namespace sys::testing::internal
