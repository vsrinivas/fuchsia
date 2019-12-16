// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// While not much will work if launchpad isn't already working, this test
// provides a place for testing aspects of launchpad that aren't necessarily
// normally used.

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <elfload/elfload.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <zxtest/zxtest.h>

// argv[0]
static const char* program_path;

#if __has_feature(address_sanitizer)
#if __has_feature(undefined_behavior_sanitizer)
#define LIBPREFIX "/boot/lib/asan-ubsan/"
#else
#define LIBPREFIX "/boot/lib/asan/"
#endif
#else
#define LIBPREFIX "/boot/lib/"
#endif

static const char dynld_path[] = LIBPREFIX "ld.so.1";

static const char test_inferior_child_name[] = "inferior";

TEST(LaunchpadTest, Basic) {
  launchpad_t* lp = NULL;

  zx_handle_t fdio_job = zx_job_default();
  ASSERT_NE(fdio_job, ZX_HANDLE_INVALID, "no fdio job object");

  zx_handle_t job_copy = ZX_HANDLE_INVALID;
  ASSERT_OK(zx_handle_duplicate(fdio_job, ZX_RIGHT_SAME_RIGHTS, &job_copy),
            "zx_handle_duplicate failed");

  zx_status_t status = launchpad_create(job_copy, test_inferior_child_name, &lp);
  ASSERT_OK(status, "launchpad_create");

  zx_handle_t vmo;
  ASSERT_OK(launchpad_vmo_from_file(program_path, &vmo));
  status = launchpad_elf_load(lp, vmo);
  ASSERT_OK(status, "launchpad_elf_load");

  zx_vaddr_t base, entry;
  status = launchpad_get_base_address(lp, &base);
  ASSERT_OK(status, "launchpad_get_base_address");
  status = launchpad_get_entry_address(lp, &entry);
  ASSERT_OK(status, "launchpad_get_entry_address");
  ASSERT_GT(base, 0u, "base > 0");

  zx_handle_t dynld_vmo = ZX_HANDLE_INVALID;
  ASSERT_OK(launchpad_vmo_from_file(dynld_path, &dynld_vmo));
  ASSERT_NE(dynld_vmo, ZX_HANDLE_INVALID, "launchpad_vmo_from_file");
  elf_load_header_t header;
  uintptr_t phoff;
  status = elf_load_prepare(dynld_vmo, NULL, 0, &header, &phoff);
  ASSERT_OK(status, "elf_load_prepare");
  printf("entry %p, base %p, header entry %p\n", (void*)entry, (void*)base, (void*)header.e_entry);
  ASSERT_EQ(entry, base + header.e_entry, "bad value for base or entry");
  zx_handle_close(dynld_vmo);

  launchpad_destroy(lp);
}

void RunOneArgumentSizeTest(size_t size) {
  launchpad_t* lp;
  ASSERT_OK(launchpad_create(ZX_HANDLE_INVALID, "argument size test", &lp));

  char* big = static_cast<char*>(malloc(size + 3));
  big[0] = ':';
  big[1] = ' ';
  memset(&big[2], 'x', size);
  big[2 + size] = '\0';
  const char* const argv[] = {"/boot/bin/sh", "-c", big};
  EXPECT_OK(launchpad_set_args(lp, fbl::count_of(argv), argv));
  free(big);

  EXPECT_OK(launchpad_load_from_file(lp, argv[0]));

  zx_handle_t proc = ZX_HANDLE_INVALID;
  const char* errmsg = "???";
  EXPECT_OK(launchpad_go(lp, &proc, &errmsg), "%s", errmsg);

  EXPECT_OK(zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL));
  zx_info_process_t info;
  EXPECT_OK(zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL));
  EXPECT_OK(zx_handle_close(proc));

  EXPECT_EQ(info.return_code, 0, "shell exit status");
}

TEST(LaunchPadTest, ArgumentSize) {
  for (size_t size = 0; size < 2 * PAGE_SIZE; size += 1024) {
    ASSERT_NO_FAILURES(RunOneArgumentSizeTest(size), "argument size is %-29zu", size);
  }
}

void RunWithArgsEnvHandles(unsigned int num_args, unsigned int num_env, unsigned int num_handles) {
  launchpad_t* lp;
  ASSERT_OK(launchpad_create(ZX_HANDLE_INVALID, "limits test", &lp));
  auto destroy_launchpad = fbl::MakeAutoCall([&]() { launchpad_destroy(lp); });

  // Set the args.
  const unsigned int argc = 3 + num_args;
  fbl::Array<const char*> argv(new const char*[argc], argc);
  argv[0] = "/boot/bin/sh";
  argv[1] = "-c";
  argv[2] = ":";
  for (unsigned int i = 3; i < argc; ++i) {
    argv[i] = "-v";
  }
  ASSERT_OK(launchpad_set_args(lp, argc, argv.data()), "%s", launchpad_error_message(lp));
  ASSERT_OK(launchpad_load_from_file(lp, argv[0]), "%s", launchpad_error_message(lp));

  // Set the env.
  //
  // Be sure to save room for a terminating null pointer.
  num_env++;
  fbl::Array<const char*> envp(new const char*[num_env], num_env);
  for (unsigned int i = 0; i < num_env; ++i) {
    envp[i] = "A=B";
  }
  envp[num_env - 1] = NULL;
  ASSERT_OK(launchpad_set_environ(lp, envp.data()), "%s", launchpad_error_message(lp));

  // Set some handles.
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(0, 0, &vmo);
  ASSERT_OK(status);
  for (unsigned int i = 0; i < num_handles; ++i) {
    zx::vmo vmo_dup;
    zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
    ASSERT_OK(status);
    ASSERT_OK(launchpad_add_handle(lp, vmo_dup.release(), PA_HND(PA_USER0, i)), "%s",
              launchpad_error_message(lp));
  }

  // Run it.
  zx::handle proc;
  const char* err = "unknown error";
  destroy_launchpad.cancel();
  ASSERT_OK(launchpad_go(lp, proc.reset_and_get_address(), &err), "%s", err);

  // See that it completed successfully.
  ASSERT_OK(zx_object_wait_one(proc.get(), ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL));
  zx_info_process_t info;
  ASSERT_OK(zx_object_get_info(proc.get(), ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL));
  ASSERT_EQ(info.return_code, 0, "shell exit status");
}

TEST(LaunchpadTest, Limits) {
  ASSERT_NO_FAILURES(RunWithArgsEnvHandles(1, 1, 1));
  ASSERT_NO_FAILURES(RunWithArgsEnvHandles(10000, 1, 1));
  ASSERT_NO_FAILURES(RunWithArgsEnvHandles(1, 10000, 1));
  ASSERT_NO_FAILURES(RunWithArgsEnvHandles(58, 58, 58));
  ASSERT_NO_FAILURES(RunWithArgsEnvHandles(1, 1, 58));
  ASSERT_NO_FAILURES(RunWithArgsEnvHandles(5000, 10000, 0));
  ASSERT_NO_FAILURES(RunWithArgsEnvHandles(5000, 10000, 58));
}

TEST(LaunchpadTest, ProcessCreateFailure) {
  launchpad_t* lp;
  EXPECT_STATUS(launchpad_create_with_jobs(ZX_HANDLE_INVALID, ZX_HANDLE_INVALID, "", &lp),
                ZX_ERR_BAD_HANDLE);
  EXPECT_STR_EQ(launchpad_error_message(lp), "create: zx_process_create() failed");
  launchpad_destroy(lp);
}

// Providing our own main() because we want to pass argv[0] to one of the tests via program_path.
int main(int argc, char** argv) {
  program_path = argv[0];
  return RUN_ALL_TESTS(argc, argv);
}
