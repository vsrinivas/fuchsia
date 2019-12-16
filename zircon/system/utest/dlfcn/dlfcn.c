// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/io.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/dlfcn.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <ldmsg/ldmsg.h>
#include <loader-service/loader-service.h>
#include <unittest/unittest.h>

#if __has_feature(address_sanitizer)
#if __has_feature(undefined_behavior_sanitizer)
#define LIBPREFIX "/boot/lib/asan-ubsan/"
#else
#define LIBPREFIX "/boot/lib/asan/"
#endif
#else
#define LIBPREFIX "/boot/lib/"
#endif

zx_status_t load_vmo(const char* filename, zx_handle_t* out) {
  int fd = open(filename, O_RDONLY);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t vmo;
  zx_handle_t exec_vmo;
  zx_status_t status = fdio_get_vmo_clone(fd, &vmo);
  close(fd);

  if (status != ZX_OK) {
    return status;
  }

  status = zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &exec_vmo);
  if (status != ZX_OK) {
    zx_handle_close(vmo);
    return status;
  }

  if (strlen(filename) >= ZX_MAX_NAME_LEN) {
    const char* p = strrchr(filename, '/');
    if (p != NULL) {
      filename = p + 1;
    }
  }

  status = zx_object_set_property(exec_vmo, ZX_PROP_NAME, filename, strlen(filename));
  if (status != ZX_OK) {
    zx_handle_close(exec_vmo);
    return status;
  }

  *out = exec_vmo;
  return ZX_OK;
}

bool dlopen_vmo_test(void) {
  BEGIN_TEST;

  zx_handle_t vmo = ZX_HANDLE_INVALID;
  zx_status_t status = load_vmo(LIBPREFIX "libtrace-engine.so", &vmo);
  EXPECT_EQ(status, ZX_OK, "");
  EXPECT_NE(vmo, ZX_HANDLE_INVALID, "load_vmo");

  void* obj = dlopen_vmo(vmo, RTLD_LOCAL);
  EXPECT_NONNULL(obj, "dlopen_vmo");

  zx_handle_close(vmo);

  void* sym = dlsym(obj, "trace_engine_initialize");
  EXPECT_NONNULL(sym, "dlsym");

  int ok = dlclose(obj);
  EXPECT_EQ(ok, 0, "dlclose");

  END_TEST;
}

// This should be some library that this program links against.
#define TEST_SONAME "libfdio.so"
#define TEST_NAME "foobar"
#define TEST_ACTUAL_NAME LIBPREFIX TEST_SONAME

static atomic_bool my_loader_service_ok = false;
static atomic_int my_loader_service_calls = 0;

static zx_status_t my_load_object(void* ctx, const char* name, zx_handle_t* out) {
  ++my_loader_service_calls;

  int cmp = strcmp(name, TEST_NAME);
  EXPECT_EQ(cmp, 0, "called with unexpected name");
  if (cmp != 0) {
    unittest_printf("        saw \"%s\", expected \"%s\"", name, TEST_NAME);
    return ZX_HANDLE_INVALID;
  }

  zx_handle_t vmo = ZX_HANDLE_INVALID;
  zx_status_t status = load_vmo((char*)ctx, &vmo);
  EXPECT_EQ(status, ZX_OK, "");
  EXPECT_NE(vmo, ZX_HANDLE_INVALID, "load_vmo");
  if (status < 0) {
    return status;
  }

  my_loader_service_ok = true;
  *out = vmo;
  return ZX_OK;
}

static zx_status_t my_load_abspath(void* ctx, const char* name, zx_handle_t* vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t my_publish_data_sink(void* ctx, const char* name, zx_handle_t vmo) {
  zx_handle_close(vmo);
  return ZX_ERR_NOT_SUPPORTED;
}

static loader_service_ops_t my_loader_ops = {
    .load_object = my_load_object,
    .load_abspath = my_load_abspath,
    .publish_data_sink = my_publish_data_sink,
};

static void show_dlerror(void) { unittest_printf_critical("dlerror: %s\n", dlerror()); }

bool loader_service_test(void) {
  BEGIN_TEST;

  // Get a handle to an existing library with a known SONAME.
  void* by_name = dlopen(TEST_SONAME, RTLD_NOLOAD);
  EXPECT_NONNULL(by_name, "dlopen failed on " TEST_SONAME);
  if (by_name == NULL)
    show_dlerror();

  // Spin up our test service.
  loader_service_t* svc = NULL;
  zx_status_t status = loader_service_create(NULL, &my_loader_ops, (void*)TEST_ACTUAL_NAME, &svc);
  EXPECT_EQ(status, ZX_OK, "loader_service_create");

  zx_handle_t my_service = ZX_HANDLE_INVALID;
  status = loader_service_connect(svc, &my_service);
  EXPECT_EQ(status, ZX_OK, "loader_service_connect");

  // Install the service.
  zx_handle_t old = dl_set_loader_service(my_service);
  EXPECT_NE(old, ZX_HANDLE_INVALID, "dl_set_loader_service");

  // Now to a lookup that should go through our service.  It
  // should load up the new copy of the file, find that its
  // SONAME matches an existing library, and just return it.
  void* via_service = dlopen(TEST_NAME, RTLD_LOCAL);

  EXPECT_EQ(my_loader_service_calls, 1, "loader-service not called exactly once");

  EXPECT_NONNULL(via_service, "dlopen via service");
  if (via_service == NULL)
    show_dlerror();

  EXPECT_TRUE(my_loader_service_ok, "loader service thread not happy");

  // It should not just have succeeded, but gotten the very
  // same handle as the by-name lookup.
  EXPECT_TRUE(via_service == by_name, "dlopen via service");

  int fail = dlclose(by_name);
  EXPECT_EQ(fail, 0, "dlclose on by-name");
  if (fail)
    show_dlerror();

  fail = dlclose(via_service);
  EXPECT_EQ(fail, 0, "dlclose on via-service");
  if (fail)
    show_dlerror();

  // Put things back to how they were.
  zx_handle_t old2 = dl_set_loader_service(old);
  EXPECT_EQ(old2, my_service, "unexpected previous service handle");
  zx_handle_close(old2);

  END_TEST;
}

bool clone_test(void) {
  BEGIN_TEST;

  zx_handle_t h = ZX_HANDLE_INVALID;
  zx_status_t s = dl_clone_loader_service(&h);
  EXPECT_EQ(s, ZX_OK, zx_status_get_string(s));
  EXPECT_NE(h, ZX_HANDLE_INVALID, "invalid handle from loader service");

  zx_handle_close(h);

  END_TEST;
}

void test_global_function(void) {}

static bool dladdr_unexported_test(void) {
  BEGIN_TEST;

  Dl_info info;
  ASSERT_NE(dladdr(&test_global_function, &info), 0, "dladdr failed");

  // This symbol is not exported to .dynsym, so it won't be found.
  EXPECT_NULL(info.dli_sname, "unexpected symbol name");
  EXPECT_NULL(info.dli_saddr, "unexpected symbol address");

  END_TEST;
}

// TODO(dbort): Test that this process uses the system loader service by default

bool dso_no_note_test(void) {
  BEGIN_TEST;

  void* obj = dlopen("test-dso-no-note.so", RTLD_LOCAL);
  ASSERT_NONNULL(obj, dlerror());

  void* sym = dlsym(obj, "dummy");
  EXPECT_NONNULL(sym, dlerror());

  (*(void(*)(void))(uintptr_t)(sym))();

  EXPECT_EQ(dlclose(obj), 0, dlerror());

  END_TEST;
}

BEGIN_TEST_CASE(dlfcn_tests)
RUN_TEST(dlopen_vmo_test);
RUN_TEST(loader_service_test);
RUN_TEST(clone_test);
RUN_TEST(dladdr_unexported_test);
RUN_TEST(dso_no_note_test);
END_TEST_CASE(dlfcn_tests)
