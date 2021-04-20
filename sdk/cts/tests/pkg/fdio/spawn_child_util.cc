// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/utc.h>

static bool has_fd(int fd) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_fd_clone(fd, &handle);
  if (status == ZX_OK) {
    zx_handle_close(handle);
    return true;
  }
  return false;
}

static bool has_ns(const char* path) {
  zx_handle_t h1, h2;
  zx_status_t status = zx_channel_create(0, &h1, &h2);
  if (status != ZX_OK)
    return false;
  status = fdio_service_connect(path, h1);
  zx_handle_close(h2);
  return status == ZX_OK;
}

static bool has_arg(uint32_t arg) { return zx_take_startup_handle(arg) != ZX_HANDLE_INVALID; }

static int check_flags(uint32_t flags, int success) {
  // We can't actually load the process without FDIO_SPAWN_DEFAULT_LDSVC, so we
  // always add it into the flags.
  flags |= FDIO_SPAWN_DEFAULT_LDSVC;

  bool should_have_job = (flags & FDIO_SPAWN_CLONE_JOB) != 0;
  bool has_job = zx_job_default() != ZX_HANDLE_INVALID;
  if (has_job != should_have_job)
    return -1;

  bool should_have_ldsvc = (flags & FDIO_SPAWN_DEFAULT_LDSVC) != 0;
  zx_handle_t ldsvc;
  bool has_ldsvc = dl_clone_loader_service(&ldsvc) != ZX_ERR_UNAVAILABLE;
  if (has_ldsvc != should_have_ldsvc)
    return -2;

  bool should_have_namespace = (flags & FDIO_SPAWN_CLONE_NAMESPACE) != 0;
  fdio_flat_namespace_t* flat = NULL;
  if (fdio_ns_export_root(&flat) != ZX_OK)
    return -3;
  bool has_namespace = flat->count > 0;
  fdio_ns_free_flat_ns(flat);
  if (has_namespace != should_have_namespace)
    return -4;

  bool should_have_stdio = (flags & FDIO_SPAWN_CLONE_STDIO) != 0;
  bool has_stdio = has_fd(0) || has_fd(1) || has_fd(2);
  if (has_stdio != should_have_stdio)
    return -5;

  bool should_have_environ = (flags & FDIO_SPAWN_CLONE_ENVIRON) != 0;
  bool has_environ = environ[0] != NULL;
  if (has_environ != should_have_environ)
    return -6;

  return success;
}

static bool check_env(const char* name, const char* expected) {
  const char* actual = getenv(name);
  if (!actual)
    return false;
  return !strcmp(actual, expected);
}

static bool do_stat(const char* path) {
  struct stat statbuf;
  return stat(path, &statbuf) == 0;
}

int do_spawn(int argc, char** argv) {
  zx::handle subprocess;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                                  subprocess.reset_and_get_address());
  if (status != ZX_OK)
    return status;

  status = subprocess.wait_one(ZX_TASK_TERMINATED, zx::time(ZX_TIME_INFINITE), nullptr);
  if (status != ZX_OK)
    return -102;

  size_t actual, avail;
  zx_info_process_t proc_info;
  status = subprocess.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), &actual, &avail);
  if (status != ZX_OK)
    return -103;
  if (actual != 1)
    return -104;

  return int(proc_info.return_code);
}

zx_koid_t koid_of_global_utc_clock() {
  zx_handle_t clock_handle = zx_utc_reference_get();
  if (clock_handle == ZX_HANDLE_INVALID) {
    return ZX_KOID_INVALID;
  }
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(clock_handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }
  return info.koid;
}

int main(int argc, char** argv) {
  if (argc == 0)
    return 42;
  if (argc == 1)
    return 43;
  const char* cmd = argv[1];
  if (!strcmp(cmd, "--argc"))
    return argc;
  if (!strcmp(cmd, "--flags")) {
    if (argc != 3)
      return -251;
    const char* flags = argv[2];
    if (!strcmp(flags, "none"))
      return check_flags(0, 51);
    if (!strcmp(flags, "job"))
      return check_flags(FDIO_SPAWN_CLONE_JOB, 52);
    if (!strcmp(flags, "namespace"))
      return check_flags(FDIO_SPAWN_CLONE_NAMESPACE, 53);
    if (!strcmp(flags, "stdio"))
      return check_flags(FDIO_SPAWN_CLONE_STDIO, 54);
    if (!strcmp(flags, "environ"))
      return check_flags(FDIO_SPAWN_CLONE_ENVIRON, 55);
    if (!strcmp(flags, "all"))
      return check_flags(FDIO_SPAWN_CLONE_ALL, 56);
  }
  if (!strcmp(cmd, "--env")) {
    if (argc != 3)
      return -252;
    const char* env = argv[2];
    if (!strcmp(env, "empty"))
      return environ[0] == NULL ? 61 : -1;
    if (!strcmp(env, "one")) {
      bool pass =
          environ[0] != NULL && !strcmp(environ[0], "SPAWN_TEST_CHILD=1") && environ[1] == NULL;
      return pass ? 62 : -2;
    }
    if (!strcmp(env, "two")) {
      bool pass = environ[0] != NULL && !strcmp(environ[0], "SPAWN_TEST_CHILD=1") &&
                  environ[1] != NULL && !strcmp(environ[1], "SPAWN_TEST_CHILD2=1") &&
                  environ[2] == NULL;
      return pass ? 63 : -3;
    }
    if (!strcmp(env, "clone")) {
      bool pass = check_env("SPAWN_TEST_PARENT", "1");
      return pass ? 64 : -4;
    }
  }
  if (!strcmp(cmd, "--action")) {
    if (argc != 3)
      return -252;
    const char* action = argv[2];
    if (!strcmp(action, "clone-fd"))
      return has_fd(21) && !has_fd(22) ? 71 : -1;
    if (!strcmp(action, "transfer-fd"))
      return has_fd(21) && !has_fd(22) ? 72 : -2;
    if (!strcmp(action, "clone-and-transfer-fd"))
      return has_fd(21) && has_fd(22) && !has_fd(23) ? 73 : -3;
    if (!strcmp(action, "ns-entry"))
      return has_ns("/foo/bar/baz") && !has_ns("/baz/bar/foo") ? 74 : -4;
    if (!strcmp(action, "add-handle"))
      return has_arg(PA_USER0) && !has_arg(PA_USER1) ? 75 : -5;
    if (!strcmp(action, "add-handle-clock-utc"))
      return static_cast<int>(koid_of_global_utc_clock());
  }
  if (!strcmp(cmd, "--stat")) {
    if (argc != 3)
      return -253;
    return do_stat(argv[2]) ? 76 : -6;
  }
  if (!strcmp(cmd, "--spawn")) {
    if (argc < 3)
      return -254;
    return do_spawn(argc - 2, argv + 2);
  }

  return -250;
}
