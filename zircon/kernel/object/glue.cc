// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file defines:
// * Initialization code for kernel/object module
// * Singleton instances and global locks
// * Helper functions

#include <inttypes.h>
#include <lib/cmdline.h>
#include <lib/crashlog.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <lk/init.h>
#include <object/diagnostics.h>
#include <object/event_dispatcher.h>
#include <object/executor.h>
#include <object/job_dispatcher.h>
#include <object/memory_watchdog.h>
#include <object/port_dispatcher.h>
#include <platform/crashlog.h>
#include <platform/halt_helper.h>

static Executor gExecutor;

fbl::RefPtr<JobDispatcher> GetRootJobDispatcher() { return gExecutor.GetRootJobDispatcher(); }

fbl::RefPtr<EventDispatcher> GetMemPressureEvent(uint32_t kind) {
  return gExecutor.GetMemPressureEvent(kind);
}

static void object_glue_init(uint level) TA_NO_THREAD_SAFETY_ANALYSIS {
  Handle::Init();
  PortDispatcher::Init();

  gExecutor.Init();
}

LK_INIT_HOOK(libobject, object_glue_init, LK_INIT_LEVEL_THREADING)
