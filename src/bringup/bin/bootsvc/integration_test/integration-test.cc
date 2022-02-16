// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/boot/c/fidl.h>
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
#include <unistd.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/system.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "../util.h"

namespace {

namespace fio = fuchsia_io;

static std::vector<std::string> arguments;

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

// Consumes the given fd and returns the result of a call to fuchsia.io.Node.GetFlags.
uint32_t fd_get_flags(fbl::unique_fd fd) {
  zx::channel file_channel;
  if (fdio_fd_transfer(fd.release(), file_channel.reset_and_get_address()) != ZX_OK) {
    return 0;
  }

  fidl::WireSyncClient<fio::Node> client(std::move(file_channel));
  auto result = client->GetFlags();
  if (result.status() != ZX_OK) {
    return 0;
  }
  fidl::WireResponse<fio::Node::GetFlags>* response = result.Unwrap();
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

// Make sure that bootsvc gave us a namespace with only /boot.
TEST(BootsvcIntegrationTest, Namespace) {
  fdio_flat_namespace_t* ns;
  ASSERT_EQ(fdio_ns_export_root(&ns), ZX_OK);

  // Close the cloned handles, since we don't need them
  for (size_t i = 0; i < ns->count; ++i) {
    zx_handle_close(ns->handle[i]);
  }

  ASSERT_EQ(ns->count, 1);
  EXPECT_STREQ(ns->path[0], "/boot");
  free(ns);

  // /boot should be RX and /svc should be RW. The call to fdio_open_fd should fail if that is not
  // the case, but we also use fuchsia.io.Node.GetFlags to validate the returned set of rights.
  fbl::unique_fd fd;
  EXPECT_EQ(ZX_OK,
            fdio_open_fd("/boot", fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable,
                         fd.reset_and_get_address()));
  EXPECT_EQ(fd_get_flags(std::move(fd)),
            fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable);
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
      "/boot/kernel/vdso/stable",      "/boot/kernel/vdso/next",
  };
  for (const char* file : kExecutableFiles) {
    fbl::unique_fd fd;
    EXPECT_EQ(ZX_OK,
              fdio_open_fd(file, fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable,
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
              fdio_open_fd(file, fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable,
                           fd.reset_and_get_address()),
              "open %s exec", file);
    EXPECT_EQ(ZX_OK, fdio_open_fd(file, fio::wire::kOpenRightReadable, fd.reset_and_get_address()),
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

// Bootfs times should always be zero rather than following a system UTC thats isn't always
// available or reliable early in boot.
TEST(BootsvcIntegrationTest, BootfsFileTimes) {
  const char* kTestPath = "/boot/pkg";
  fbl::unique_fd fd;
  struct stat s;

  ASSERT_EQ(ZX_OK,
            fdio_open_fd(kTestPath, fio::wire::kOpenRightReadable, fd.reset_and_get_address()),
            "open %s for file time check", kTestPath);
  ASSERT_EQ(0, fstat(fd.get(), &s), "get %s attributes", kTestPath);
  EXPECT_EQ(0, s.st_ctim.tv_sec);
  EXPECT_EQ(0, s.st_mtim.tv_sec);
}

// Make sure that bootsvc passed along program arguments from bootsvc.next
// correctly.
//
// As documented in TESTING, this test relies on these tests being run by using
// a boot cmdline that includes 'bootsvc.next=bin/bootsvc-integration-test,testargument' so
// that we can test the parsing on bootsvc.next.
TEST(BootsvcIntegrationTest, Arguments) {
  ASSERT_EQ(arguments.size(), 2);
  EXPECT_STREQ(arguments[0].c_str(), "bin/bootsvc-integration-test");
  EXPECT_STREQ(arguments[1].c_str(), "testargument");
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

// Test that we can get the resources passed from the kernel.
TEST(BootsvcIntegrationTest, ResourcesAvailable) {
  zx::resource mmio_resource(zx_take_startup_handle(PA_HND(PA_MMIO_RESOURCE, 0)));
  ASSERT_TRUE(mmio_resource.is_valid());
  zx::resource irq_resource(zx_take_startup_handle(PA_HND(PA_IRQ_RESOURCE, 0)));
  ASSERT_TRUE(irq_resource.is_valid());
  zx::resource system_resource(zx_take_startup_handle(PA_HND(PA_SYSTEM_RESOURCE, 0)));
  ASSERT_TRUE(system_resource.is_valid());
#if __x86_64__
  zx::resource ioport_resource(zx_take_startup_handle(PA_HND(PA_IOPORT_RESOURCE, 0)));
  ASSERT_TRUE(ioport_resource.is_valid());
#elif __aarch64__
  zx::resource smc_resource(zx_take_startup_handle(PA_HND(PA_SMC_RESOURCE, 0)));
  ASSERT_TRUE(smc_resource.is_valid());
#endif
  zx::resource root_resource(zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)));
  ASSERT_TRUE(root_resource.is_valid());
}

}  // namespace

int main(int argc, char** argv) {
  // Copy arguments for later use in tests.
  for (int i = 0; i < argc; ++i) {
    arguments.push_back(std::string(argv[i]));
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
