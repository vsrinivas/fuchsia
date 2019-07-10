// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ldmsg/ldmsg.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <loader-service/loader-service.h>
#include <zircon/dlfcn.h>
#include <zircon/processargs.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

#include <atomic>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

namespace {

constexpr char kTestSinkName[] = "test-sink";
constexpr char kTestConfigGoodName[] = "/test-config-exists";
constexpr char kTestConfigBadName[] = "/test-config-does-not-exist";

std::atomic_bool my_loader_service_has_failures;
std::atomic_int my_loader_service_calls;

zx_status_t my1_load_object(void* ctx, const char* name, zx_handle_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t my1_load_abspath(void* ctx, const char* name, zx_handle_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t my1_publish_data_sink(void* ctx, const char* name, zx_handle_t handle) {
  ++my_loader_service_calls;

  zx::vmo vmo(handle);

  EXPECT_STR_EQ(kTestSinkName, name, "called with unexpected name");

  EXPECT_NE(vmo, ZX_HANDLE_INVALID, "called with invalid handle");

  char vmo_name[ZX_MAX_NAME_LEN];
  EXPECT_OK(vmo.get_property(ZX_PROP_NAME, vmo_name, sizeof(vmo_name)));
  EXPECT_STR_EQ(kTestSinkName, vmo_name, "not called with expected VMO handle");

  vmo.reset();

  my_loader_service_has_failures = CURRENT_TEST_HAS_FAILURES();
  return ZX_OK;
}

loader_service_ops_t my1_loader_ops = {
    .load_object = my1_load_object,
    .load_abspath = my1_load_abspath,
    .publish_data_sink = my1_publish_data_sink,
    .finalizer = nullptr,
};

}  // namespace

TEST(SanitizerUtilsTest, PublishData) {
  my_loader_service_has_failures = false;
  my_loader_service_calls = 0;

  // Spin up our test service.
  loader_service_t* svc = nullptr;
  EXPECT_OK(loader_service_create(nullptr, &my1_loader_ops, nullptr, &svc));

  zx::channel my_service;
  EXPECT_OK(loader_service_connect(svc, my_service.reset_and_get_address()));

  // Install the service.
  zx::channel old(dl_set_loader_service(my_service.get()));
  EXPECT_NE(old, ZX_HANDLE_INVALID, "dl_set_loader_service");

  // Make up a VMO to publish.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  EXPECT_OK(vmo.set_property(ZX_PROP_NAME, kTestSinkName, sizeof(kTestSinkName)));

  // Publish the VMO to our data sink.
  __sanitizer_publish_data(kTestSinkName, vmo.release());

  EXPECT_EQ(my_loader_service_calls, 1, "loader-service not called exactly once");

  EXPECT_FALSE(my_loader_service_has_failures, "loader service thread not happy");

  // Put things back to how they were.
  zx::channel old2(dl_set_loader_service(old.release()));
  EXPECT_EQ(old2, my_service, "unexpected previous service handle");
  old2.reset();
}

namespace {

zx::vmo test_config_vmo;

zx_status_t my2_load_object(void* ctx, const char* name, zx_handle_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t my2_load_abspath(void* ctx, const char* name, zx_handle_t* out) {
  ++my_loader_service_calls;

  zx_handle_t result = ZX_HANDLE_INVALID;
  if (!strcmp(kTestConfigGoodName, name)) {
    EXPECT_NE(test_config_vmo, ZX_HANDLE_INVALID);
    *out = test_config_vmo.get();
    result = ZX_OK;
  } else {
    EXPECT_STR_EQ(kTestConfigBadName, name, "called with unexpected name");
    result = ZX_ERR_NOT_FOUND;
  }

  my_loader_service_has_failures = CURRENT_TEST_HAS_FAILURES();
  return result;
}

zx_status_t my2_publish_data_sink(void* ctx, const char* name, zx_handle_t vmo) {
  zx_handle_close(vmo);
  return ZX_ERR_NOT_SUPPORTED;
}

loader_service_ops_t my2_loader_ops = {
    .load_object = my2_load_object,
    .load_abspath = my2_load_abspath,
    .publish_data_sink = my2_publish_data_sink,
    .finalizer = nullptr,
};

}  // namespace

TEST(SanitzerUtilsTest, DebugConfig) {
  my_loader_service_has_failures = false;
  my_loader_service_calls = 0;

  // Spin up our test service.
  loader_service_t* svc = nullptr;
  EXPECT_OK(loader_service_create(nullptr, &my2_loader_ops, nullptr, &svc));

  zx::channel my_service;
  EXPECT_OK(loader_service_connect(svc, my_service.reset_and_get_address()));

  // Install the service.
  zx::channel old(dl_set_loader_service(my_service.get()));
  EXPECT_NE(old, ZX_HANDLE_INVALID, "dl_set_loader_service");

  // Make up a VMO that we'll get back from the service.
  ASSERT_OK(zx::vmo::create(0, 0, &test_config_vmo));

  // Test the success case.
  zx::vmo vmo;
  EXPECT_OK(__sanitizer_get_configuration(kTestConfigGoodName, vmo.reset_and_get_address()),
            "__sanitizer_get_configuration on valid name");
  EXPECT_EQ(vmo, test_config_vmo, "not the expected VMO handle");

  EXPECT_EQ(my_loader_service_calls, 1, "loader-service not called exactly once");
  EXPECT_FALSE(my_loader_service_has_failures, "loader service thread not happy");

  test_config_vmo.reset();

  my_loader_service_has_failures = false;
  my_loader_service_calls = 0;

  // Test the failure case.
  EXPECT_EQ(__sanitizer_get_configuration(kTestConfigBadName, vmo.reset_and_get_address()),
            ZX_ERR_NOT_FOUND, "__sanitizer_get_configuration on invalid name");

  EXPECT_EQ(my_loader_service_calls, 1, "loader-service not called exactly once");
  EXPECT_FALSE(my_loader_service_has_failures, "loader service thread not happy");

  // Put things back to how they were.
  zx::channel old2(dl_set_loader_service(old.release()));
  EXPECT_EQ(old2, my_service, "unexpected previous service handle");
  old2.reset();
}

#if __has_feature(address_sanitizer)
TEST(SanitzerUtilsTest, FillShadow) {
  zx_info_task_stats_t task_stats;

  // Snapshot the memory use at the beginning.
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));

  size_t init_mem_use = task_stats.mem_private_bytes;

  const size_t len = 32 * PAGE_SIZE;

  // Allocate some memory...
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  uintptr_t addr;
  ASSERT_OK(zx::vmar::root_self()->map(0, vmo, 0, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr));

  size_t alloc_mem_use = task_stats.mem_private_bytes;

  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));

  EXPECT_GE(alloc_mem_use, init_mem_use, "");

  // ..and poison it.
  ASAN_POISON_MEMORY_REGION((void*)addr, len);

  // Snapshot the memory use after the allocation.
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));

  size_t memset_mem_use = task_stats.mem_private_bytes;

  // We expect the memory use to go up.
  EXPECT_GT(memset_mem_use, alloc_mem_use, "");

  // Unpoison the shadow.
  __sanitizer_fill_shadow(addr, len, 0, 0);

  // Snapshot the memory use after unpoisoning.
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));

  size_t fill_shadow_mem_use = task_stats.mem_private_bytes;

  // We expect the memory use to decrease.
  EXPECT_LT(fill_shadow_mem_use, memset_mem_use, "");

  // Deallocate the memory.
  ASSERT_OK(zx::vmar::root_self()->unmap(addr, len));
}
#endif
