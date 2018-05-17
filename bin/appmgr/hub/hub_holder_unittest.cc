// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/hub/realm_hub.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <zx/channel.h>

#include "gtest/gtest.h"

namespace component {
namespace {

TEST(RealmHub, Simple) {
  RealmHub hub(fbl::AdoptRef(new fs::PseudoDir()));

  fbl::RefPtr<fs::Vnode> realm_dir;
  fbl::RefPtr<fs::Vnode> component_dir;
  ASSERT_EQ(hub.dir()->Lookup(&realm_dir, "r"), ZX_OK);
  ASSERT_EQ(hub.dir()->Lookup(&component_dir, "c"), ZX_OK);

  auto test_realm_dir = fbl::AdoptRef(new fs::PseudoDir());
  fbl::String test_realm_name = "test-realm";
  fbl::String test_realm_koid = "1028";
  auto hub_info = HubInfo(test_realm_name, test_realm_koid, test_realm_dir);
  ASSERT_EQ(hub.AddRealm(hub_info), ZX_OK);

  fbl::RefPtr<fs::Vnode> name_dir;
  ASSERT_EQ(realm_dir->Lookup(&name_dir, test_realm_name), ZX_OK);

  fbl::RefPtr<fs::Vnode> koid_dir;
  ASSERT_EQ(name_dir->Lookup(&koid_dir, test_realm_koid), ZX_OK);

  // test that currect vnode was added for hub
  test_realm_dir->AddEntry("test-dir", fbl::AdoptRef(new fs::PseudoDir()));

  fbl::RefPtr<fs::Vnode> test_dir;
  ASSERT_EQ(koid_dir->Lookup(&test_dir, "test-dir"), ZX_OK);

  // test realm removal
  ASSERT_EQ(hub.RemoveRealm(hub_info), ZX_OK);
  ASSERT_EQ(realm_dir->Lookup(&name_dir, test_realm_name), ZX_ERR_NOT_FOUND);
}

}  // namespace
}  // namespace component
