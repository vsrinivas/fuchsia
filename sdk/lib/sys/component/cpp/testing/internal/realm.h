// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_REALM_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_REALM_H_

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <string>

namespace component_testing {
namespace internal {

fuchsia::component::RealmSyncPtr CreateRealmPtr(const sys::ComponentContext* context);
fuchsia::component::RealmSyncPtr CreateRealmPtr(std::shared_ptr<sys::ServiceDirectory> svc);
sys::ServiceDirectory OpenExposedDir(fuchsia::component::Realm_Sync* realm,
                                     const fuchsia::component::decl::ChildRef& child_ref);

void CreateChild(fuchsia::component::Realm_Sync* realm, std::string collection, std::string name,
                 std::string url);

void DestroyChild(fuchsia::component::Realm_Sync* realm,
                  fuchsia::component::decl::ChildRef child_ref);
void DestroyChild(fuchsia::component::Realm* realm, fuchsia::component::decl::ChildRef child_ref);

}  // namespace internal
}  // namespace component_testing

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_REALM_H_
