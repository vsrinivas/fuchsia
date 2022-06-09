// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_SESSION_SESSION_H_
#define SRC_MODULAR_LIB_SESSION_SESSION_H_

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/fpromise/result.h>

namespace modular::session {

// Connects to the BasemgrDebug protocol served by the currently running instance of basemgr.
//
// # Errors
//
// ZX_ERR_NOT_FOUND: basemgr is not running or service connection failed
fpromise::result<fuchsia::modular::internal::BasemgrDebugPtr, zx_status_t> ConnectToBasemgrDebug();

}  // namespace modular::session

#endif  // SRC_MODULAR_LIB_SESSION_SESSION_H_
