// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>
#include <zircon/types.h>

#include <lib/zx/job.h>

// Initialize the crash service, this supersedes the standalone service with
// the same name that lived in zircon/system/core/crashsvc/crashsvc.cpp
// (/boot/bin/crashsvc) and ad-hoc microservice in devmgr that delegated to
// svchost. See ZX-3199 for details.
//
// The job of this service is to handle exceptions that reached |root_job| and
// delegate the crash analysis to one of two services:
//
// - built-in : using system/ulib/inspector
// - appmgr hosted: via FIDL interface call (fuchsia_crash_Analyzer).
//
// Which one depends if |analyzer_svc| is a valid channel handle, which
// svchost sets depending on "use_system".
//
// The crash service thread will exit when |root_job| is terminated.
//
// On success, returns ZX_OK and fills |thread| with the crash service thread.
// The caller is responsible for either detaching or joining the thread.
zx_status_t start_crashsvc(zx::job root_job, zx_handle_t analyzer_svc, thrd_t* thread);
