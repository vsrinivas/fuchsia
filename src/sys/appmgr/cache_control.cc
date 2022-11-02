// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/cache_control.h"

#include <lib/inspect/cpp/vmo/types.h>
#include <lib/trace/event.h>

#include "src/sys/appmgr/constants.h"
#include "src/sys/appmgr/storage_watchdog.h"

namespace component {

CacheControl::CacheControl() = default;
CacheControl::~CacheControl() = default;

void CacheControl::AddBinding(fidl::InterfaceRequest<fuchsia::sys::test::CacheControl> request) {
  bindings_.AddBinding(this, std::move(request));
}

void CacheControl::Clear(ClearCallback callback) {
  auto cc_trace_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("appmgr", "CacheControl::Clear", cc_trace_id);

  StorageWatchdog storage_watchdog = StorageWatchdog(inspect::Node(), kRootDataDir, kRootCacheDir);
  storage_watchdog.PurgeCache();

  callback();
}

}  // namespace component
