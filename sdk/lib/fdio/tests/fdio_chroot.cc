// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/processargs.h>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

static void spawn_child(const char** argv, std::string* out_stdout) {
  zx::socket stdout_parent, stdout_child;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &stdout_parent, &stdout_child));

  const fdio_spawn_action_t actions[] = {
      {
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_FD, STDOUT_FILENO),
                  .handle = stdout_child.release(),
              },
      },
  };
  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  ASSERT_OK(fdio_spawn_etc(
                ZX_HANDLE_INVALID,
                FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_NAMESPACE | FDIO_SPAWN_CLONE_UTC_CLOCK,
                argv[0], argv, nullptr, sizeof(actions) / sizeof(fdio_spawn_action_t), actions,
                process.reset_and_get_address(), err_msg),
            "%s", err_msg);

  // Wait for the process to exit.
  ASSERT_OK(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));

  char buffer[32 * 1024];
  size_t actual = 0u;
  ASSERT_OK(stdout_parent.read(0, buffer, sizeof(buffer), &actual));
  *out_stdout = std::string(buffer, actual);
}

static auto prepare_directories() {
  mkdir("/tmp/chroot1", 0666);
  mkdir("/tmp/chroot1/a", 0666);
  mkdir("/tmp/chroot1/a/foo", 0666);
  mkdir("/tmp/chroot1/aa", 0666);
  mkdir("/tmp/chroot1/b", 0666);
  return fbl::MakeAutoCall([] {
    rmdir("/tmp/chroot1/b");
    rmdir("/tmp/chroot1/a/foo");
    rmdir("/tmp/chroot1/a");
    rmdir("/tmp/chroot1/aa");
    rmdir("/tmp/chroot1");
  });
}

// chroot to / without changing anything.
TEST(ChrootTest, Slash) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1/a", "/", "/tmp/chroot1", nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1/a) SUCCESS\n"
      "chroot(/) SUCCESS\n"
      "access(/tmp/chroot1) SUCCESS\n"
      "cwd=/tmp/chroot1/a\n"
      "realpath=/tmp/chroot1/a\n",
      result.c_str());
}

// Basic smoke test of a normal chroot operation.
TEST(ChrootTest, Smoke) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1/a", "/tmp/chroot1", "/a", nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1/a) SUCCESS\n"
      "chroot(/tmp/chroot1) SUCCESS\n"
      "access(/a) SUCCESS\n"
      "cwd=/a\n"
      "realpath=/a\n",
      result.c_str());
}

// chroot to a relative path above the current working directory.
TEST(ChrootTest, AboveCWD) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1/a", "..", "/a", nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1/a) SUCCESS\n"
      "chroot(..) SUCCESS\n"
      "access(/a) SUCCESS\n"
      "cwd=/a\n"
      "realpath=/a\n",
      result.c_str());
}

// chroot to a mount point in the local namespace.
TEST(ChrootTest, MountPoint) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1", "/tmp", "/chroot1/a", nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1) SUCCESS\n"
      "chroot(/tmp) SUCCESS\n"
      "access(/chroot1/a) SUCCESS\n"
      "cwd=/chroot1\n"
      "realpath=/chroot1\n",
      result.c_str());
}

// chroot to a location that does not contain the current working directory.
TEST(ChrootTest, AwayFromCWD) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1", "/tmp/chroot1/a", "/foo", nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1) SUCCESS\n"
      "chroot(/tmp/chroot1/a) SUCCESS\n"
      "access(/foo) SUCCESS\n"
      "cwd=(unreachable)\n"
      "realpath=(unreachable)\n",
      result.c_str());
}

// Check that we don't mistakenly think that /tmp/chroot1/a is a path-prefix of /tmp/chroot1/aa.
TEST(ChrootTest, TrickyPathPrefix) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1/aa", "/tmp/chroot1/a", "/foo",
                        nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1/aa) SUCCESS\n"
      "chroot(/tmp/chroot1/a) SUCCESS\n"
      "access(/foo) SUCCESS\n"
      "cwd=(unreachable)\n"
      "realpath=(unreachable)\n",
      result.c_str());
}

// Access a file outside of the chroot through the current working directory.
TEST(ChrootTest, AccessOutsideRoot) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1", "a", "b", nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1) SUCCESS\n"
      "chroot(a) SUCCESS\n"
      "access(b) SUCCESS\n"
      "cwd=(unreachable)\n"
      "realpath=(unreachable)\n",
      result.c_str());
}

// chroot to a bogus location.
TEST(ChrootTest, BogusDirectory) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1", "/bogus", "/tmp/chroot1", nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1) SUCCESS\n"
      "chroot returned -1, errno=2\n",
      result.c_str());
}

TEST(ChrootTest, CannotEscapeWithDotDot) {
  auto clean_dir = prepare_directories();
  std::string result;
  const char* argv[] = {"/pkg/bin/chroot-child", "/tmp/chroot1", "/tmp/chroot1", "/../chroot1", nullptr};
  ASSERT_NO_FAILURES(spawn_child(argv, &result));
  EXPECT_STR_EQ(
      "chdir(/tmp/chroot1) SUCCESS\n"
      "chroot(/tmp/chroot1) SUCCESS\n"
      "access returned -1, errno=22\n",
      result.c_str());
}
