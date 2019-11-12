// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fs-pty/service.h>
#include <lib/zx/eventpair.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

#include "pty-server-vnode.h"
#include "pty-server.h"

// Each Open() on this Vnode redirects to a new PtyServerVnode
class PtyGeneratingVnode : public fs::Vnode {
 public:
  PtyGeneratingVnode(fs::Vfs* vfs) : vfs_(vfs) {}
  ~PtyGeneratingVnode() override = default;

  zx_status_t GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                     [[maybe_unused]] fs::Rights rights,
                                     fs::VnodeRepresentation* info) override {
    // This should only actually be seen by something querying with VNODE_REF_ONLY.
    *info = fs::VnodeRepresentation::Tty{.event = {}};
    return ZX_OK;
  }

  fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kTty; }

  zx_status_t Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) override {
    fbl::RefPtr<PtyServer> server;
    zx_status_t status = PtyServer::Create(&server, vfs_);
    if (status != ZX_OK) {
      return status;
    }
    *out_redirect = fbl::MakeRefCounted<PtyServerVnode>(std::move(server));
    return ZX_OK;
  }

 private:
  fs::Vfs* vfs_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  fs::SynchronousVfs vfs(dispatcher);
  zx_status_t status;

  auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  root_dir->AddEntry("svc", svc_dir);

  auto dir_request = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  if (!dir_request) {
    printf("console: failed to take startup handle\n");
    return -1;
  }
  if ((status = vfs.ServeDirectory(root_dir, std::move(dir_request))) != ZX_OK) {
    printf("console: failed to serve startup handle: %s\n", zx_status_get_string(status));
    return -1;
  }

  svc_dir->AddEntry("fuchsia.hardware.pty.Device", fbl::MakeRefCounted<PtyGeneratingVnode>(&vfs));

  status = loop.Run();
  ZX_ASSERT(status == ZX_OK);
  return 0;
}
