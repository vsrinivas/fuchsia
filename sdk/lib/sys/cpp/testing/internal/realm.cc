// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/internal/errors.h>
#include <lib/sys/cpp/testing/internal/realm.h>

namespace sys::testing::internal {

fuchsia::sys2::RealmSyncPtr CreateRealmPtr(const sys::ComponentContext* context) {
  ASSERT_NOT_NULL(context);
  fuchsia::sys2::RealmSyncPtr realm;
  context->svc()->Connect(realm.NewRequest());
  return realm;
}

ServiceDirectory OpenExposedDir(fuchsia::sys2::Realm_Sync* realm,
                                const fuchsia::sys2::ChildRef& child_ref) {
  ASSERT_NOT_NULL(realm);
  fuchsia::io::DirectorySyncPtr exposed_dir;
  fuchsia::sys2::Realm_OpenExposedDir_Result result;
  ASSERT_STATUS_AND_RESULT_OK("Realm/OpenExposedDir",
                              realm->OpenExposedDir(child_ref, exposed_dir.NewRequest(), &result),
                              result);
  return ServiceDirectory(std::move(exposed_dir));
}

void CreateChild(fuchsia::sys2::Realm_Sync* realm, std::string collection, std::string name,
                 std::string url) {
  ASSERT_NOT_NULL(realm);
  fuchsia::sys2::CollectionRef collection_ref = {
      .name = collection,
  };
  fuchsia::sys2::ChildDecl child_decl;
  child_decl.set_name(name);
  child_decl.set_url(url);
  child_decl.set_startup(fuchsia::sys2::StartupMode::LAZY);
  fuchsia::sys2::Realm_CreateChild_Result result;
  ASSERT_STATUS_AND_RESULT_OK("Realm/CreateChild",
                              realm->CreateChild(std::move(collection_ref), std::move(child_decl),
                                                 fuchsia::sys2::CreateChildArgs{}, &result),
                              result);
}

void DestroyChild(fuchsia::sys2::Realm_Sync* realm, fuchsia::sys2::ChildRef child_ref) {
  ASSERT_NOT_NULL(realm);
  fuchsia::sys2::Realm_DestroyChild_Result result;
  ASSERT_STATUS_AND_RESULT_OK("Realm/DestroyChild",
                              realm->DestroyChild(std::move(child_ref), &result), result);
}

}  // namespace sys::testing::internal
