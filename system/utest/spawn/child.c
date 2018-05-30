// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fdio/limits.h>
#include <fdio/namespace.h>
#include <fdio/spawn.h>
#include <fdio/util.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>

static bool has_fd(int fd) {
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t ids[FDIO_MAX_HANDLES];
    return fdio_clone_fd(fd, fd + 50, handles, ids) > 0;
}

static int check_flags(uint32_t flags, int success) {
    // We can't actually load the process without FDIO_SPAWN_CLONE_LDSVC, so we
    // always add it into the flags.
    flags |= FDIO_SPAWN_CLONE_LDSVC;

    bool should_have_job = (flags & FDIO_SPAWN_SHARE_JOB) != 0;
    bool has_job = zx_job_default() != ZX_HANDLE_INVALID;
    if (has_job != should_have_job)
        return -1;

    bool should_have_ldsvc = (flags & FDIO_SPAWN_CLONE_LDSVC) != 0;
    zx_handle_t ldsvc;
    bool has_ldsvc = dl_clone_loader_service(&ldsvc) != ZX_ERR_UNAVAILABLE;
    if (has_ldsvc != should_have_ldsvc)
        return -2;

    bool should_have_namespace = (flags & FDIO_SPAWN_CLONE_NAMESPACE) != 0;
    fdio_ns_t* ns = NULL;
    bool has_namespace = fdio_ns_get_installed(&ns) != ZX_ERR_NOT_FOUND;
    if (has_namespace != should_have_namespace)
        return -3;

    bool should_have_stdio = (flags & FDIO_SPAWN_CLONE_STDIO) != 0;
    bool has_stdio = has_fd(0) || has_fd(1) || has_fd(2);
    if (has_stdio != should_have_stdio)
        return -4;

    bool should_have_environ = (flags & FDIO_SPAWN_CLONE_ENVIRON) != 0;
    bool has_environ = environ[0] != NULL;
    if (has_environ != should_have_environ)
        return -5;

    return success;
}

static bool check_env(const char* name, const char* expected) {
    const char* actual = getenv(name);
    if (!actual)
        return false;
    return !strcmp(actual, expected);
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
            return check_flags(FDIO_SPAWN_SHARE_JOB, 52);
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
            bool pass = environ[0] != NULL && !strcmp(environ[0], "SPAWN_TEST_CHILD=1") &&
                        environ[1] == NULL;
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
            return fcntl(21, F_GETFD) >= 0 ? 71 : -1;
        if (!strcmp(action, "clone-fd"))
            return fcntl(22, F_GETFD) >= 0 ? 72 : -2;
    }

    return -250;
}
