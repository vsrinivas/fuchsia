// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/io.h>
#include <launchpad/launchpad.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

static zx_status_t Launch(const char* name,
                          int argc,
                          const char* const* argv,
                          zx_handle_t* handles,
                          uint32_t* types,
                          size_t hcount,
                          zx::handle* proc) {
  zx::handle invalid, job_copy;
  invalid.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE,
                    &job_copy);

  launchpad_t* lp;
  launchpad_create(job_copy.get(), name, &lp);
  launchpad_load_from_file(lp, argv[0]);
  launchpad_set_args(lp, argc, argv);
  launchpad_clone(lp, LP_CLONE_ALL);

  zx_status_t status;
  zx::handle h;
  if ((status = zx_log_create(0, h.reset_and_get_address()) < 0)) {
    launchpad_abort(lp, status, "devmgr: cannot create debuglog handle");
  } else {
    launchpad_add_handle(lp, h.release(),
                         PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO | 0));
  }

  if (hcount > 0)
    launchpad_add_handles(lp, hcount, handles, types);

  const char* errmsg;
  if ((status = launchpad_go(lp, proc->reset_and_get_address(), &errmsg)) < 0) {
    printf("crashpad_startup: launchpad %s (%s) failed: %s: %d\n", argv[0],
           name, errmsg, status);
  } else {
    printf("crashpad_startup: launch %s (%s) OK\n", argv[0], name);
  }
  return status;
}

int main(int argc, char* const argv[]) {
  // Opt-in to crash reporting using crashpad_database_util.
  static const char* argv_crashpad_database_util[] = {
      "/system/bin/crashpad_database_util", "--database=/data/crashes",
      "--create", "--set-uploads-enabled=true",
  };
  printf("WARNING: In test configuration, opting in to crash report upload.\n");
  zx::handle proc;
  zx_status_t status =
      Launch("crashpad_database_util", countof(argv_crashpad_database_util),
             argv_crashpad_database_util, NULL, NULL, 0, &proc);
  if (status != ZX_OK) {
    printf("crashpad_startup: crashpad_database_util failed %d\n", status);
    return 1;
  }
  proc.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr);

  // Pass on handle 0 and 1 from devmgr.
  zx_handle_t handles[] = {zx_get_startup_handle(PA_HND(PA_USER0, 0)),
                           zx_get_startup_handle(PA_HND(PA_USER0, 1))};
  uint32_t handle_types[] = {PA_HND(PA_USER0, 0), PA_HND(PA_USER0, 1)};
  static const char* argv_crashpad_handler[] = {
      "/system/bin/crashpad_handler",
      "--database=/data/crashes",
      "--url=http://clients2.google.com/cr/report",
      "--annotation=product=Fuchsia",
      "--annotation=version=unknown",
  };
  char version[64] = {};
  if (zx_system_get_version(version, sizeof(version)) == ZX_OK) {
    static char verarg[128];
    snprintf(verarg, sizeof(verarg), "--annotation=version=%s", version);
    argv_crashpad_handler[4] = verarg;
  }

  status = Launch("crashpad_handler", countof(argv_crashpad_handler),
                  argv_crashpad_handler, handles, handle_types,
                  countof(handles), &proc);
  if (status != ZX_OK) {
    printf("crashpad_startup: crashpad_handler failed %d\n", status);
    return 1;
  }

  return 0;
}
