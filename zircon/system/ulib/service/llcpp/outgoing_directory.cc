// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/service/llcpp/outgoing_directory.h>
#include <lib/service/llcpp/service_handler.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <fbl/ref_counted.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace service {

namespace {

fbl::RefPtr<fs::PseudoDir> FindDir(fbl::RefPtr<fs::PseudoDir> root, cpp17::string_view name) {
  fbl::RefPtr<fs::Vnode> node;
  zx_status_t result = root->Lookup(name, &node);
  if (result != ZX_OK || !node->Supports(fs::VnodeProtocol::kDirectory)) {
    return {};
  }
  return fbl::RefPtr<fs::PseudoDir>::Downcast(node);
}

fbl::RefPtr<fs::PseudoDir> FindOrCreateDir(fbl::RefPtr<fs::PseudoDir> root,
                                           cpp17::string_view name) {
  auto dir = FindDir(root, name);
  if (dir == nullptr) {
    dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root->AddEntry(name, dir);
  }
  return dir;
}

}  // namespace

OutgoingDirectory::OutgoingDirectory(async_dispatcher_t* dispatcher)
    : vfs_(dispatcher),
      root_(fbl::MakeRefCounted<fs::PseudoDir>()),
      svc_(fbl::MakeRefCounted<fs::PseudoDir>()),
      debug_(fbl::MakeRefCounted<fs::PseudoDir>()) {
  root_->AddEntry("svc", svc_);
  root_->AddEntry("debug", debug_);
}

zx::status<> OutgoingDirectory::Serve(fidl::ServerEnd<fuchsia_io::Directory> directory_request) {
  zx_status_t status = vfs_.ServeDirectory(root_, std::move(directory_request));
  return zx::make_status(status);
}

zx::status<> OutgoingDirectory::ServeFromStartupInfo() {
  fidl::ServerEnd<fuchsia_io::Directory> directory_request(
      zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));
  return Serve(std::move(directory_request));
}

zx::status<> OutgoingDirectory::AddNamedService(ServiceHandler handler, cpp17::string_view service,
                                                cpp17::string_view instance) const {
  zx_status_t status = FindOrCreateDir(svc_, service)->AddEntry(instance, handler.TakeDirectory());
  return zx::make_status(status);
}

zx::status<> OutgoingDirectory::RemoveNamedService(cpp17::string_view service,
                                                   cpp17::string_view instance) const {
  fbl::RefPtr<fs::PseudoDir> service_dir = FindDir(svc_, service);
  if (service_dir == nullptr) {
    return zx::error(ZX_ERR_IO_INVALID);
  }
  zx_status_t status = service_dir->RemoveEntry(instance);
  return zx::make_status(status);
}

}  // namespace service
