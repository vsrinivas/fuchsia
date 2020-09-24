// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/userabi/userboot_internal.h>

#include <object/dispatcher.h>
#include <object/handle.h>
#include <object/resource.h>
#include <object/resource_dispatcher.h>

namespace {

bool GetRangedResourceTest() {
  BEGIN_TEST;
  HandleOwner rsrc_handle = get_resource_handle(ZX_RSRC_KIND_MMIO);
  auto rsrc_dispatcher = DownCastDispatcher<ResourceDispatcher>(rsrc_handle->dispatcher().get());

  ASSERT_TRUE(rsrc_dispatcher->get_kind() == ZX_RSRC_KIND_MMIO);
  ASSERT_TRUE(rsrc_dispatcher->get_base() == 0);
  ASSERT_TRUE(rsrc_dispatcher->get_size() == 0);
  ASSERT_TRUE(rsrc_dispatcher->IsRangedRoot(ZX_RSRC_KIND_MMIO));

  rsrc_handle = get_resource_handle(ZX_RSRC_KIND_VMEX);
  rsrc_dispatcher = DownCastDispatcher<ResourceDispatcher>(rsrc_handle->dispatcher().get());

  ASSERT_TRUE(rsrc_dispatcher->get_kind() == ZX_RSRC_KIND_VMEX);
  ASSERT_TRUE(rsrc_dispatcher->get_base() == 0);
  ASSERT_TRUE(rsrc_dispatcher->get_size() == 0);
  ASSERT_FALSE(rsrc_dispatcher->IsRangedRoot(ZX_RSRC_KIND_VMEX));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(userboot_tests)
UNITTEST("get_ranged_resource", GetRangedResourceTest)
UNITTEST_END_TESTCASE(userboot_tests, "userboot", "userboot tests")
