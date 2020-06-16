// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_PTYSVC_PTY_SERVER_VNODE_H_
#define ZIRCON_SYSTEM_CORE_PTYSVC_PTY_SERVER_VNODE_H_

#include <lib/fs-pty/service.h>

#include <fbl/ref_ptr.h>

#include "pty-server-device.h"
#include "pty-server.h"

// Vnode representing a single pty server.  It will live as long as there are
// active connections to the server.
using PtyServerVnodeBase =
    fs_pty::Service<PtyServerDevice, fs_pty::SimpleConsoleOps<fbl::RefPtr<PtyServer>>,
                    fbl::RefPtr<PtyServer>>;
class PtyServerVnode : public PtyServerVnodeBase {
 public:
  explicit PtyServerVnode(fbl::RefPtr<PtyServer> console)
      : PtyServerVnodeBase(std::move(console)) {}

  ~PtyServerVnode() override { console_->Shutdown(); }
};

#endif  // ZIRCON_SYSTEM_CORE_PTYSVC_PTY_SERVER_VNODE_H_
