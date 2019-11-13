// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/system.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>

#include "../util.h"

namespace fio = ::llcpp::fuchsia::io;

static fbl::Vector<fbl::String> arguments;

constexpr char kArgumentsPath[] = "/svc/" fuchsia_boot_Arguments_Name;
constexpr char kFactoryItemsPath[] = "/svc/" fuchsia_boot_FactoryItems_Name;
constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;
constexpr char kReadOnlyLogPath[] = "/svc/" fuchsia_boot_ReadOnlyLog_Name;
constexpr char kRootJobPath[] = "/svc/" fuchsia_boot_RootJob_Name;
constexpr char kRootJobForInspectPath[] = "/svc/" fuchsia_boot_RootJobForInspect_Name;
constexpr char kRootResourcePath[] = "/svc/" fuchsia_boot_RootResource_Name;
constexpr char kWriteOnlyLogPath[] = "/svc/" fuchsia_boot_WriteOnlyLog_Name;

[[noreturn]] void poweroff() {
  // Grab the root resource, needed to make the poweroff call.
  // We ignore returned status codes; there's nothing useful for us to do in
  // the event of a failure.
  zx::channel local, remote;
  zx::channel::create(0, &local, &remote);
  fdio_service_connect(kRootResourcePath, remote.release());
  zx::resource root_resource;
  fuchsia_boot_RootResourceGet(local.get(), root_resource.reset_and_get_address());

  // Power off.
  zx_system_powerctl(root_resource.get(), ZX_SYSTEM_POWERCTL_SHUTDOWN, NULL);

  while (true) {
    __builtin_trap();
  }
}

void print_test_success_string() {
  // Get the debuglog handle.
  // If any of these operations fail, there's nothing we can really do here, so
  // just move along.
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return;
  }
  status = fdio_service_connect(kWriteOnlyLogPath, remote.release());
  if (status != ZX_OK) {
    return;
  }

  zx::debuglog log;
  status = fuchsia_boot_WriteOnlyLogGet(local.get(), log.reset_and_get_address());
  if (status != ZX_OK) {
    return;
  }

  // print the success string to the debug log
  zx_debuglog_write(log.get(), 0, ZBI_TEST_SUCCESS_STRING, sizeof(ZBI_TEST_SUCCESS_STRING));
}

int main(int argc, char** argv) {
  // Copy arguments for later use in tests.
  for (int i = 0; i < argc; ++i) {
    arguments.push_back(fbl::String(argv[i]));
  }

  int result = unittest_run_all_tests(argc, argv) ? 0 : -1;
  if (result == 0) {
    print_test_success_string();
  }

  // Sleep 3 seconds to allow buffers to flush before powering off
  zx::nanosleep(zx::deadline_after(zx::sec(3)));

  // Exit.  This won't actually return.
  poweroff();

  return result;
}

namespace {

// Make sure the loader works
bool TestLoader() {
  BEGIN_TEST;

  // Request loading a library we don't use
  void* ptr = dlopen("libdriver.so", RTLD_LAZY | RTLD_LOCAL);
  ASSERT_NOT_NULL(ptr);
  dlclose(ptr);

  END_TEST;
}

// Make sure that bootsvc gave us a namespace with only /boot and /svc.
bool TestNamespace() {
  BEGIN_TEST;

  fdio_flat_namespace_t* ns;
  ASSERT_EQ(fdio_ns_export_root(&ns), ZX_OK);

  // Close the cloned handles, since we don't need them
  for (size_t i = 0; i < ns->count; ++i) {
    zx_handle_close(ns->handle[i]);
  }

  ASSERT_EQ(ns->count, 2);
  EXPECT_STR_EQ(ns->path[0], "/boot");
  EXPECT_STR_EQ(ns->path[1], "/svc");
  free(ns);

  // /boot should be RX and /svc should be RW. This uses a roundabout way to check connection rights
  // on a fuchsia.io.Directory, since GetFlags is only on fuchsia.io/File
  // TODO(fxb/37419): Once fuchsia.io/Node supports GetFlags, we should update this to use that
  // instead of just testing rights through a Directory.Open
  int fd;
  EXPECT_EQ(ZX_OK,
            fdio_open_fd("/boot", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE, &fd));
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, fdio_open_fd("/boot",
                                               fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE |
                                                   fio::OPEN_RIGHT_EXECUTABLE,
                                               &fd));
  EXPECT_EQ(ZX_OK, fdio_open_fd("/svc", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, &fd));
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED,
            fdio_open_fd("/svc", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE, &fd));

  END_TEST;
}

// Make sure that bootsvc passed along program arguments from bootsvc.next
// correctly.
//
// As documented in TESTING, this test relies on these tests being run by using
// a boot cmdline that includes 'bootsvc.next=bin/bootsvc-integration-test,testargument' so
// that we can test the parsing on bootsvc.next.
bool TestArguments() {
  BEGIN_TEST;

  ASSERT_EQ(arguments.size(), 2);
  EXPECT_STR_EQ(arguments[0].c_str(), "bin/bootsvc-integration-test");
  EXPECT_STR_EQ(arguments[1].c_str(), "testargument");

  END_TEST;
}

// Make sure the fuchsia.boot.Arguments service works
bool TestBootArguments() {
  BEGIN_TEST;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.Arguments service.
  status = fdio_service_connect(kArgumentsPath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we received a VMO from the service, each time we call it.
  for (size_t i = 0; i < 8; i++) {
    zx::vmo vmo;
    size_t size;
    status = fuchsia_boot_ArgumentsGet(local.get(), vmo.reset_and_get_address(), &size);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(vmo.is_valid());

    // Check that the VMO is read-only.
    zx_info_handle_basic_t info;
    status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHT_WRITE, info.rights);
  }

  END_TEST;
}

// Make sure the fuchsia.boot.FactoryItems service works
bool TestFactoryItems() {
  BEGIN_TEST;

  zx::channel local_items, remote_items;
  zx_status_t status = zx::channel::create(0, &local_items, &remote_items);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.Items service.
  status = fdio_service_connect(kItemsPath, remote_items.release());
  ASSERT_EQ(ZX_OK, status);

  zx::vmo payload;
  uint32_t length;

  // No factory items should appear here.
  status = fuchsia_boot_ItemsGet(local_items.get(), ZBI_TYPE_STORAGE_BOOTFS_FACTORY, 0,
                                 payload.reset_and_get_address(), &length);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_FALSE(payload.is_valid());
  ASSERT_EQ(0, length);

  zx::channel local_factory_items, remote_factory_items;
  status = zx::channel::create(0, &local_factory_items, &remote_factory_items);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.FactoryItems service.
  status = fdio_service_connect(kFactoryItemsPath, remote_factory_items.release());
  ASSERT_EQ(ZX_OK, status);

  static constexpr char kExpected[] = "IAmAFactoryItemHooray";
  uint8_t buf[sizeof(kExpected) - 1];

  // Verify that multiple calls work.
  for (int i = 0; i < 2; i++) {
    status = fuchsia_boot_FactoryItemsGet(local_factory_items.get(), 0,
                                          payload.reset_and_get_address(), &length);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(payload.is_valid());
    ASSERT_EQ(sizeof(buf), length);
    ASSERT_EQ(ZX_OK, payload.read(buf, 0, sizeof(buf)));
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(kExpected), buf, sizeof(buf), "");
  }
  END_TEST;
}

// Make sure that bootsvc parsed and passed boot args from ZBI_ITEM_IMAGE_ARGS
// correctly.
bool TestBootArgsFromImage() {
  BEGIN_TEST;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.Arguments service.
  status = fdio_service_connect(kArgumentsPath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we received a VMO from the service, each time we call it.
  zx::vmo vmo;
  size_t size;
  status = fuchsia_boot_ArgumentsGet(local.get(), vmo.reset_and_get_address(), &size);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(vmo.is_valid());

  auto buf = std::make_unique<char[]>(size);
  status = vmo.read(buf.get(), 0, size);
  ASSERT_EQ(ZX_OK, status);

  /* Boot args from Image are at the beginning of Arguments VMO */
  static constexpr char kExpected[] = "testkey=testvalue";
  auto actual = reinterpret_cast<const uint8_t*>(buf.get());
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(kExpected), actual, sizeof(kExpected) - 1, "");

  END_TEST;
}

// Make sure the fuchsia.boot.Items service works
bool TestBootItems() {
  BEGIN_TEST;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.Items service.
  status = fdio_service_connect(kItemsPath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we can get the following boot item types.
  uint32_t types[] = {
      ZBI_TYPE_CRASHLOG,
      ZBI_TYPE_PLATFORM_ID,
      ZBI_TYPE_STORAGE_RAMDISK,
  };
  for (uint32_t type : types) {
    zx::vmo payload;
    uint32_t length;
    status = fuchsia_boot_ItemsGet(local.get(), type, 0, payload.reset_and_get_address(), &length);
    ASSERT_EQ(ZX_OK, status);

#ifdef __x64_64__
    // (The following is only implemented on x64 at this time, so we only test
    // it there.)
    // If we see a ZBI_TYPE_CRASHLOG item, then the kernel should have
    // translated it into a VMO file, and bootsvc should have put it at the
    // path below.
    if (type == ZBI_TYPE_CRASHLOG) {
      ASSERT_TRUE(payload.is_valid());
      fbl::String path = fbl::StringPrintf("/boot/%s", bootsvc::kLastPanicFilePath);
      fbl::unique_fd fd(open(path.data(), O_RDONLY));
      ASSERT_TRUE(fd.is_valid());

      auto file_buf = std::make_unique<uint8_t[]>(length);
      auto payload_buf = std::make_unique<uint8_t[]>(length);
      ASSERT_EQ(length, read(fd.get(), file_buf.get(), length));
      ASSERT_EQ(ZX_OK, payload.read(payload_buf.get(), 0, length));
      ASSERT_BYTES_EQ(file_buf.get(), payload_buf.get(), length, "");
    }
#endif
  }

  END_TEST;
}

// Make sure the fuchsia.boot.WriteOnlyLog service works
bool TestBootWriteOnlyLog() {
  BEGIN_TEST;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.WriteOnlyLog service.
  status = fdio_service_connect(kWriteOnlyLogPath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we received a debuglog from the service.
  zx::debuglog log;
  status = fuchsia_boot_WriteOnlyLogGet(local.get(), log.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(log.is_valid());

  // Check that the handle is writable and not readable.
  zx_info_handle_basic_t info;
  ASSERT_EQ(ZX_OK, log.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  ASSERT_TRUE(info.rights & ZX_RIGHT_WRITE);
  ASSERT_FALSE(info.rights & ZX_RIGHT_READ);

  END_TEST;
}

// Make sure the fuchsia.boot.ReadOnlyLog service works
bool TestBootReadOnlyLog() {
  BEGIN_TEST;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.ReadOnlyLog service.
  status = fdio_service_connect(kReadOnlyLogPath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we received a debuglog from the service.
  zx::debuglog log;
  status = fuchsia_boot_ReadOnlyLogGet(local.get(), log.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(log.is_valid());

  // Check that the handle is readable and not writable.
  zx_info_handle_basic_t info;
  ASSERT_EQ(ZX_OK, log.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  ASSERT_TRUE(info.rights & ZX_RIGHT_READ);
  ASSERT_FALSE(info.rights & ZX_RIGHT_WRITE);

  END_TEST;
}

// Make sure the fuchsia.boot.RootJob service works
bool TestBootRootJob() {
  BEGIN_TEST;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.RootJob service.
  status = fdio_service_connect(kRootJobPath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we received a job from the service.
  zx::job root_job;
  status = fuchsia_boot_RootJobGet(local.get(), root_job.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(root_job.is_valid());

  END_TEST;
}

// Make sure the fuchsia.boot.RootJobForInspect service works
bool TestBootRootJobForInspect() {
  BEGIN_TEST;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.RootJobForInspect service.
  status = fdio_service_connect(kRootJobForInspectPath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we received a job from the service.
  zx::job root_job;
  status = fuchsia_boot_RootJobForInspectGet(local.get(), root_job.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(root_job.is_valid());
  zx_info_handle_basic_t info;
  status = root_job.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(zx_info_handle_basic_t), nullptr,
                             nullptr);
  ASSERT_EQ(ZX_OK, status, "zx_object_get_info failed");
  ASSERT_EQ(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_INSPECT | ZX_RIGHT_ENUMERATE |
                ZX_RIGHT_GET_PROPERTY,
            info.rights);

  END_TEST;
}

// Make sure the fuchsia.boot.RootResource service works
bool TestBootRootResource() {
  BEGIN_TEST;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.RootResource service.
  status = fdio_service_connect(kRootResourcePath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we received a resource from the service.
  zx::resource root_resource;
  status = fuchsia_boot_RootResourceGet(local.get(), root_resource.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(root_resource.is_valid());

  // Check that a subsequent call also results in a success.  Previous
  // versions of this service would only provide the root resource to the
  // first caller, and would close the channel thereafter.
  status = fuchsia_boot_RootResourceGet(local.get(), root_resource.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(root_resource.is_valid());

  END_TEST;
}

// Check that the kernel-provided VDSOs were added to /boot/kernel/vdso
bool TestVdsosPresent() {
  BEGIN_TEST;

  DIR* dir = opendir("/boot/kernel/vdso");
  ASSERT_NOT_NULL(dir);

  size_t count = 0;
  dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (!strcmp(entry->d_name, ".")) {
      continue;
    }
    ASSERT_EQ(entry->d_type, DT_REG);
    ++count;
  }
  ASSERT_GT(count, 0);

  closedir(dir);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(bootsvc_integration_tests)
RUN_TEST(TestLoader)
RUN_TEST(TestNamespace)
RUN_TEST(TestArguments)
RUN_TEST(TestBootArguments)
RUN_TEST(TestBootArgsFromImage)
RUN_TEST(TestBootItems)
RUN_TEST(TestBootReadOnlyLog)
RUN_TEST(TestBootRootJob)
RUN_TEST(TestBootRootJobForInspect)
RUN_TEST(TestBootRootResource)
RUN_TEST(TestBootWriteOnlyLog)
RUN_TEST(TestFactoryItems)
RUN_TEST(TestVdsosPresent)
END_TEST_CASE(bootsvc_integration_tests)
