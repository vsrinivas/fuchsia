// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/io.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/dlfcn.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

#include "src/lib/loader_service/loader_service.h"

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

TEST(DlfcnTests, dlopen_vmo_test) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  zx_status_t status = load_vmo(LIBPREFIX "libtrace-engine.so", &vmo);
  EXPECT_EQ(status, ZX_OK, "");
  EXPECT_NE(vmo, ZX_HANDLE_INVALID, "load_vmo");

  void* obj = dlopen_vmo(vmo, RTLD_LOCAL);
  EXPECT_NOT_NULL(obj, "dlopen_vmo");

  zx_handle_close(vmo);

  void* sym = dlsym(obj, "trace_engine_initialize");
  EXPECT_NOT_NULL(sym, "dlsym");

  int ok = dlclose(obj);
  EXPECT_EQ(ok, 0, "dlclose");
}

// This should be some library that this program links against.
#define TEST_SONAME "libfdio.so"
#define TEST_NAME "foobar"
#define TEST_ACTUAL_NAME LIBPREFIX TEST_SONAME

class TestLoaderService : public loader::LoaderServiceBase {
 public:
  static std::shared_ptr<TestLoaderService> Create(async_dispatcher_t* dispatcher) {
    // Can't use make_shared because constructor is private
    return std::shared_ptr<TestLoaderService>(new TestLoaderService(dispatcher));
  }

  int load_object_calls() { return load_object_calls_; }
  int load_object_success() { return load_object_success_; }

 private:
  TestLoaderService(async_dispatcher_t* dispatcher) : LoaderServiceBase(dispatcher, "dlfcn_test") {}

  virtual zx::status<zx::vmo> LoadObjectImpl(std::string name) override {
    ++load_object_calls_;

    if (name != TEST_NAME) {
      printf("loader saw \"%s\", expected \"%s\"", name.c_str(), TEST_NAME);
      return zx::error(ZX_ERR_NOT_FOUND);
    }

    zx::vmo vmo;
    zx_status_t status = load_vmo(TEST_ACTUAL_NAME, vmo.reset_and_get_address());
    if (status != ZX_OK) {
      return zx::error(status);
    }
    ++load_object_success_;
    return zx::ok(std::move(vmo));
  }

  int load_object_calls_ = 0;
  int load_object_success_ = 0;
};

static void show_dlerror(void) { printf("dlerror: %s\n", dlerror()); }

TEST(DlfcnTests, loader_service_test) {
  // Get a handle to an existing library with a known SONAME.
  void* by_name = dlopen(TEST_SONAME, RTLD_NOLOAD);
  EXPECT_NOT_NULL(by_name, "dlopen failed on " TEST_SONAME);
  if (by_name == NULL)
    show_dlerror();

  // Spin up our test service.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  auto loader = TestLoaderService::Create(loop.dispatcher());
  auto loader_conn = loader->Connect();
  ASSERT_OK(loader_conn.status_value());

  // Install the service.
  zx_handle_t old = dl_set_loader_service(loader_conn.value().release());
  EXPECT_NE(old, ZX_HANDLE_INVALID, "dl_set_loader_service");

  // Now to a lookup that should go through our service.  It
  // should load up the new copy of the file, find that its
  // SONAME matches an existing library, and just return it.
  void* via_service = dlopen(TEST_NAME, RTLD_LOCAL);

  EXPECT_NOT_NULL(via_service, "dlopen via service");
  if (via_service == NULL) {
    show_dlerror();
  }
  EXPECT_EQ(loader->load_object_calls(), 1, "loader-service not called exactly once");
  EXPECT_EQ(loader->load_object_success(), 1, "loader service call didn't succeed");

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
  zx_handle_close(old2);
}

TEST(DlfcnTests, clone_test) {
  zx_handle_t h = ZX_HANDLE_INVALID;
  zx_status_t s = dl_clone_loader_service(&h);
  EXPECT_EQ(s, ZX_OK, "%s", zx_status_get_string(s));
  EXPECT_NE(h, ZX_HANDLE_INVALID, "invalid handle from loader service");

  zx_handle_close(h);
}

void test_global_function(void) {}

TEST(DlfcnTests, dladdr_unexported_test) {
  Dl_info info;
  ASSERT_NE(dladdr((void*)&test_global_function, &info), 0, "dladdr failed");

  // This symbol is not exported to .dynsym, so it won't be found.
  EXPECT_NULL(info.dli_sname, "unexpected symbol name");
  EXPECT_NULL(info.dli_saddr, "unexpected symbol address");
}

// TODO(dbort): Test that this process uses the system loader service by default

TEST(DlfcnTests, dso_no_note_test) {
  void* obj = dlopen("test-dso-no-note.so", RTLD_LOCAL);
  ASSERT_NOT_NULL(obj, "%s", dlerror());

  void* sym = dlsym(obj, "dummy");
  EXPECT_NOT_NULL(sym, "%s", dlerror());

  (*(void (*)(void))(uintptr_t)(sym))();

  EXPECT_EQ(dlclose(obj), 0, "%s", dlerror());
}
