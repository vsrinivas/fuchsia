// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/outgoing.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace component {

Outgoing::Outgoing()
    : vfs_(async_get_default_dispatcher()),
      root_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      public_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      debug_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      ctrl_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      deprecated_outgoing_services_(public_dir_) {
  root_dir_->AddEntry("public", public_dir_);
  root_dir_->AddEntry("debug", debug_dir_);
  root_dir_->AddEntry("ctrl", ctrl_dir_);
  auto objects = Object::Make("objects");
  object_dir_ = std::make_unique<ObjectDir>(objects);

  auto out_objects = fbl::MakeRefCounted<fs::PseudoDir>();
  out_objects->AddEntry(
      fuchsia::inspect::Inspect::Name_,
      fbl::MakeRefCounted<fs::Service>([this](zx::channel chan) {
        inspect_bindings_.AddBinding(
            object_dir_->object(),
            fidl::InterfaceRequest<fuchsia::inspect::Inspect>(std::move(chan)),
            nullptr);
        return ZX_OK;
      }));
  root_dir_->AddEntry("objects", out_objects);
}

Outgoing::~Outgoing() = default;

zx_status_t Outgoing::Serve(zx::channel dir_request) {
  if (!dir_request)
    return ZX_ERR_BAD_HANDLE;
  return vfs_.ServeDirectory(root_dir_, std::move(dir_request));
}

zx_status_t Outgoing::ServeFromStartupInfo() {
  zx_handle_t dir_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  return Serve(zx::channel(dir_request));
}

}  // namespace component
