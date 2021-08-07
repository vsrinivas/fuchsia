// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_INTERNAL_REALM_H_
#define LIB_SYS_CPP_TESTING_INTERNAL_REALM_H_

#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <string>

namespace sys::testing::internal {

fuchsia::sys2::RealmSyncPtr CreateRealmPtr(const sys::ComponentContext* context);
ServiceDirectory OpenExposedDir(fuchsia::sys2::Realm_Sync* realm,
                                const fuchsia::sys2::ChildRef& child_ref);

void CreateChild(fuchsia::sys2::Realm_Sync* realm, std::string collection, std::string name,
                 std::string url);

void DestroyChild(fuchsia::sys2::Realm_Sync* realm, fuchsia::sys2::ChildRef child_ref);

}  // namespace sys::testing::internal

#endif  // LIB_SYS_CPP_TESTING_INTERNAL_REALM_H_
