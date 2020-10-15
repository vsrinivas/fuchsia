// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _GNU_SOURCE
#include <errno.h>
#include <unistd.h>

#include "libc.h"

#define ZX_PPID ((pid_t)2)
#define ZX_PID ((pid_t)3)
#define ZX_PGID ((pid_t)17)
#define ZX_SID ((pid_t)19)
#define ZX_UID ((uid_t)23)
#define ZX_GID ((gid_t)42)

static gid_t stub_getegid(void) { return ZX_GID; }
weak_alias(stub_getegid, getegid);

static gid_t stub_getgid(void) { return ZX_GID; }
weak_alias(stub_getgid, getgid);

static int stub_getgroups(int count, gid_t list[]) {
  if (!list)
    return EFAULT;
  return 0;
}
weak_alias(stub_getgroups, getgroups);

static int stub_getresgid(gid_t* rgid, gid_t* egid, gid_t* sgid) {
  if (!rgid || !egid || !sgid)
    return EFAULT;
  *rgid = *egid = *sgid = ZX_GID;
  return 0;
}
weak_alias(stub_getresgid, getresgid);

static int stub_getresuid(uid_t* ruid, uid_t* euid, uid_t* suid) {
  if (!ruid || !euid || !suid)
    return EFAULT;
  *ruid = *euid = *suid = ZX_UID;
  return 0;
}
weak_alias(stub_getresuid, getresuid);

static pid_t stub_getpgid(pid_t pid) { return ZX_PGID; }
weak_alias(stub_getpgid, getpgid);

static pid_t stub_getpgrp(void) { return ZX_PGID; }
weak_alias(stub_getpgrp, getpgrp);

static pid_t stub_getpid(void) { return ZX_PID; }
weak_alias(stub_getpid, getpid);

static pid_t stub_getppid(void) { return ZX_PPID; }
weak_alias(stub_getppid, getppid);

static pid_t stub_getsid(pid_t pid) { return ZX_SID; }
weak_alias(stub_getsid, getsid);

static uid_t stub_geteuid(void) { return ZX_UID; }
weak_alias(stub_geteuid, geteuid);

static uid_t stub_getuid(void) { return ZX_UID; }
weak_alias(stub_getuid, getuid);

static pid_t stub_setsid(void) { return ZX_SID; }
weak_alias(stub_setsid, setsid);

static int stub_setegid(gid_t egid) { return ZX_GID; }
weak_alias(stub_setegid, setegid);

static int stub_seteuid(uid_t euid) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_seteuid, seteuid);

static int stub_setgid(gid_t gid) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_setgid, setgid);

static int stub_setgroups(size_t count, const gid_t list[]) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_setgroups, setgroups);

static int stub_setpgid(pid_t pid, pid_t pgid) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_setpgid, setpgid);

static int stub_setregid(gid_t rgid, gid_t egid) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_setregid, setregid);

static int stub_setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_setresgid, setresgid);

static int stub_setresuid(uid_t ruid, uid_t euid, uid_t suid) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_setresuid, setresuid);

static int stub_setreuid(uid_t ruid, uid_t euid) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_setreuid, setreuid);

static int stub_setuid(uid_t uid) {
  errno = EPERM;
  return -1;
}
weak_alias(stub_setuid, setuid);
