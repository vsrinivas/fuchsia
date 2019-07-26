// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/cache_control.h"

#include <trace/event.h>

#include "garnet/bin/appmgr/storage_watchdog.h"

namespace component {

CacheControl::CacheControl() = default;
CacheControl::~CacheControl() = default;

void CacheControl::AddBinding(fidl::InterfaceRequest<fuchsia::sys::test::CacheControl> request) {
  bindings_.AddBinding(this, std::move(request));
}

void CacheControl::Clear(ClearCallback callback) {
  auto cc_trace_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("appmgr", "CacheControl::Clear", cc_trace_id);

  StorageWatchdog storage_watchdog = StorageWatchdog("/data", "/data/cache");
  storage_watchdog.PurgeCache();

  callback();
}

}  // namespace component
