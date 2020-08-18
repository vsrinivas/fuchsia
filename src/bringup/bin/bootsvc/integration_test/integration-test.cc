// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/system.h>

#include <cstdint>
#include <string>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "../util.h"

namespace {

namespace fio = ::llcpp::fuchsia::io;

static fbl::Vector<fbl::String> arguments;

constexpr char kFactoryItemsPath[] = "/svc/" fuchsia_boot_FactoryItems_Name;
constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

void print_test_success_string() {
  // Get the debuglog handle.
  // If any of these operations fail, there's nothing we can really do here, so
  // just move along.
  zx::debuglog log;
  zx_status_t status = zx::debuglog::create(zx::resource(), 0, &log);
  if (status != ZX_OK) {
    return;
  }

  // print the success string to the debug log
  zx_debuglog_write(log.get(), 0, ZBI_TEST_SUCCESS_STRING, sizeof(ZBI_TEST_SUCCESS_STRING));
}

// Consumes the given fd and returns the result of a call to fuchsia.io.Node.NodeGetFlags.
uint32_t fd_get_flags(fbl::unique_fd fd) {
  zx::channel file_channel;
  if (fdio_fd_transfer(fd.release(), file_channel.reset_and_get_address()) != ZX_OK) {
    return 0;
  }

  fio::Node::SyncClient client(std::move(file_channel));
  auto result = client.NodeGetFlags();
  if (result.status() != ZX_OK) {
    return 0;
  }
  fio::Node::NodeGetFlagsResponse* response = result.Unwrap();
  if (response->s != ZX_OK) {
    return 0;
  }
  return response->flags;
}

zx_rights_t get_rights(const zx::object_base& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.rights : ZX_RIGHT_NONE;
}

// Make sure the loader works
TEST(BootsvcIntegrationTest, Loader) {
  // Request loading a library we don't use
  void* ptr = dlopen("libdriver.so", RTLD_LAZY | RTLD_LOCAL);
  ASSERT_NOT_NULL(ptr);
  dlclose(ptr);
}

// Make sure that bootsvc gave us a namespace with only /boot and /svc.
TEST(BootsvcIntegrationTest, Namespace) {
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

  // /boot should be RX and /svc should be RW. We use OPEN_FLAG_POSIX + fuchsia.io.Node.NodeGetFlags
  // to check that these are also the maximum rights supported.
  fbl::unique_fd fd;
  EXPECT_EQ(ZX_OK, fdio_open_fd(
                       "/boot",
                       fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE | fio::OPEN_FLAG_POSIX,
                       fd.reset_and_get_address()));
  EXPECT_EQ(fd_get_flags(std::move(fd)), fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE);
  EXPECT_EQ(ZX_OK,
            fdio_open_fd("/svc",
                         fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_POSIX,
                         fd.reset_and_get_address()));
  EXPECT_EQ(fd_get_flags(std::move(fd)), fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE);
}

// We simply check here whether files can be opened with OPEN_RIGHT_EXECUTABLE or not.
//
// Also, this test really needs to be able to open specific files, which unfortunately means some
// specific lists of files to test against that make this a bit fragile. Opening subdirectories
// doesn't get us coverage because we only limit the rights that you can open the files with.
TEST(BootsvcIntegrationTest, BootfsExecutability) {
  const char* kExecutableFiles[] = {
      "/boot/pkg/bootsvc/bin/bootsvc", "/boot/pkg/dummy_pkg/lib/dummy.so",
      "/boot/driver/fragment.so",      "/boot/lib/dummy.so",
      "/boot/kernel/vdso/full",
  };
  for (const char* file : kExecutableFiles) {
    fbl::unique_fd fd;
    EXPECT_EQ(ZX_OK,
              fdio_open_fd(file, fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                           fd.reset_and_get_address()),
              "open %s exec", file);

    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, fdio_get_vmo_exec(fd.get(), vmo.reset_and_get_address()), "get_vmo_exec %s",
              file);
    EXPECT_TRUE(vmo.is_valid());
    EXPECT_EQ(ZX_RIGHT_EXECUTE, get_rights(vmo) & ZX_RIGHT_EXECUTE);
  }

  const char* kNonExecutableFiles[] = {
      "/boot/meta/fake.cm",
      "/boot/kernel/counters/desc",
  };
  for (const char* file : kNonExecutableFiles) {
    fbl::unique_fd fd;
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED,
              fdio_open_fd(file, fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                           fd.reset_and_get_address()),
              "open %s exec", file);
    EXPECT_EQ(ZX_OK, fdio_open_fd(file, fio::OPEN_RIGHT_READABLE, fd.reset_and_get_address()),
              "open %s read-only", file);

    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, fdio_get_vmo_clone(fd.get(), vmo.reset_and_get_address()), "get_vmo_clone %s",
              file);
    // Because of particulars of how fdio works with vmofiles, this returns INVALID_ARGS on failure
    // instead of ACCESS_DENIED (basically because it fails during a handle_replace inside fdio).
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, fdio_get_vmo_exec(fd.get(), vmo.reset_and_get_address()),
              "get_vmo_exec %s", file);
  }
}

// Make sure that bootsvc passed along program arguments from bootsvc.next
// correctly.
//
// As documented in TESTING, this test relies on these tests being run by using
// a boot cmdline that includes 'bootsvc.next=bin/bootsvc-integration-test,testargument' so
// that we can test the parsing on bootsvc.next.
TEST(BootsvcIntegrationTest, Arguments) {
  ASSERT_EQ(arguments.size(), 2);
  EXPECT_STR_EQ(arguments[0].c_str(), "bin/bootsvc-integration-test");
  EXPECT_STR_EQ(arguments[1].c_str(), "testargument");
}

// Make sure the fuchsia.boot.FactoryItems service works
TEST(BootsvcIntegrationTest, FactoryItems) {
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
}

// Make sure the fuchsia.boot.Items service works
TEST(BootsvcIntegrationTest, BootItems) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_EQ(ZX_OK, status);

  // Check that we can open the fuchsia.boot.Items service.
  status = fdio_service_connect(kItemsPath, remote.release());
  ASSERT_EQ(ZX_OK, status);

  // Check that we can get the following boot item types.
  // clang-format off
  uint32_t types[] = {
      ZBI_TYPE_CRASHLOG,
      ZBI_TYPE_PLATFORM_ID,
      ZBI_TYPE_DRV_BOARD_INFO,
      ZBI_TYPE_STORAGE_RAMDISK,
      ZBI_TYPE_SERIAL_NUMBER,
  };
  // clang-format on
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

    if (type == ZBI_TYPE_SERIAL_NUMBER) {
      ASSERT_TRUE(payload.is_valid());
      ASSERT_GE(length, 0);
      std::string serial_no(length, '\0');
      ASSERT_OK(payload.read(serial_no.data(), 0, length));
      EXPECT_EQ(length, serial_no.size());
    }
  }
}

// Make sure fuchsia.boot.Items::GetBootloaderFile works
TEST(BootsvcIntegrationTest, BootloaderFiles) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Check that we can open the fuchsia.boot.Items service.
  ASSERT_OK(fdio_service_connect(kItemsPath, remote.release()));

  constexpr char kInvalidBootloaderFileName[] = "This filename is too short";

  zx_handle_t vmo_handle;
  ASSERT_OK(fuchsia_boot_ItemsGetBootloaderFile(local.get(), kInvalidBootloaderFileName,
                                                strlen(kInvalidBootloaderFileName), &vmo_handle));
  ASSERT_EQ(ZX_HANDLE_INVALID, vmo_handle);

  constexpr char kValidBootloaderFileName[] = "This is the filename of the file!";
  constexpr char kValidBootloaderFilePayload[] = "FILE CONTENTS ARE HERE";

  ASSERT_OK(fuchsia_boot_ItemsGetBootloaderFile(local.get(), kValidBootloaderFileName,
                                                strlen(kValidBootloaderFileName), &vmo_handle));
  ASSERT_NE(ZX_HANDLE_INVALID, vmo_handle);

  zx::vmo vmo(vmo_handle);
  ASSERT_TRUE(vmo.is_valid());

  uint64_t size;
  ASSERT_OK(vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)));

  uint8_t buf[sizeof(kValidBootloaderFilePayload) - 1];
  ASSERT_EQ(size, sizeof(buf));
  ASSERT_OK(vmo.read(buf, 0, size));

  ASSERT_BYTES_EQ(buf, kValidBootloaderFilePayload, sizeof(buf));
}

// Check that the kernel-provided VDSOs were added to /boot/kernel/vdso
TEST(BootsvcIntegrationTest, VdsosPresent) {
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
}

// Test that the boot arguments are extracted from both vbmeta and bootfs.
TEST(BootsvcIntegrationTest, BootArguments) {
  EXPECT_STR_EQ(environ[0], "testkey=testvalue");
  EXPECT_STR_EQ(environ[1], "bootfskey=bootfsvalue");
}

}  // namespace

int main(int argc, char** argv) {
  // Copy arguments for later use in tests.
  for (int i = 0; i < argc; ++i) {
    arguments.push_back(fbl::String(argv[i]));
  }

  int result = RUN_ALL_TESTS(argc, argv);
  if (result == 0) {
    print_test_success_string();
  }

  // Sleep 3 seconds to allow buffers to flush before powering off
  zx::nanosleep(zx::deadline_after(zx::sec(3)));

  // Return. WAIT, how does this test manage to finish if "success" is QEMU
  // turning off? The ZBI that this is packaged on should set the
  // bootsvc.on_next_process_exit flag so that bootsvc turns the system off
  // when the "next process" (in this case, this test) exits.
  return result;
}
