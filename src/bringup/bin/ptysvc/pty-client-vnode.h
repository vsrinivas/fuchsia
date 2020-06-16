// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_PTYSVC_PTY_CLIENT_VNODE_H_
#define ZIRCON_SYSTEM_CORE_PTYSVC_PTY_CLIENT_VNODE_H_

#include <lib/fs-pty/service.h>

#include <fbl/ref_ptr.h>

#include "pty-client-device.h"
#include "pty-client.h"

// Vnode representing a single pty client.  It will live as long as there are
// active connections to the client.
using PtyClientVnodeBase =
    fs_pty::Service<PtyClientDevice, fs_pty::SimpleConsoleOps<fbl::RefPtr<PtyClient>>,
                    fbl::RefPtr<PtyClient>>;
class PtyClientVnode : public PtyClientVnodeBase {
 public:
  explicit PtyClientVnode(fbl::RefPtr<PtyClient> console)
      : PtyClientVnodeBase(std::move(console)) {}
  ~PtyClientVnode() override {
    // Clean up the PTY client
    console_->Shutdown();
  }
};

#endif  // ZIRCON_SYSTEM_CORE_PTYSVC_PTY_CLIENT_VNODE_H_
