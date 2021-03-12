// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/svc/outgoing.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <utility>

namespace svc {

Outgoing::Outgoing(async_dispatcher_t* dispatcher)
    : vfs_(dispatcher),
      root_dir_(fbl::MakeRefCounted<fs::PseudoDir>()),
      svc_dir_(fbl::MakeRefCounted<fs::PseudoDir>()) {
  root_dir_->AddEntry("svc", svc_dir_);
}

Outgoing::~Outgoing() = default;

zx_status_t Outgoing::Serve(fidl::ServerEnd<fuchsia_io::Directory> dir_server_end) {
  if (!dir_server_end.is_valid())
    return ZX_ERR_BAD_HANDLE;
  return vfs_.ServeDirectory(root_dir_, std::move(dir_server_end));
}

zx_status_t Outgoing::ServeFromStartupInfo() {
  zx_handle_t dir_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  return Serve(zx::channel(dir_request));
}

}  // namespace svc
