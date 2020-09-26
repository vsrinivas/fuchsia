// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/string_view.h>
#include <lib/service/llcpp/outgoing_directory.h>
#include <lib/service/llcpp/service_handler.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <fbl/ref_counted.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

namespace llcpp::sys {

namespace {

fbl::RefPtr<fs::PseudoDir> FindDir(fbl::RefPtr<fs::PseudoDir> root, fit::string_view name) {
  fbl::RefPtr<fs::Vnode> node;
  zx_status_t result = root->Lookup(name, &node);
  if (result != ZX_OK || !node->Supports(fs::VnodeProtocol::kDirectory)) {
    return {};
  }
  return fbl::RefPtr<fs::PseudoDir>::Downcast(node);
}

fbl::RefPtr<fs::PseudoDir> FindOrCreateDir(fbl::RefPtr<fs::PseudoDir> root, fit::string_view name) {
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

zx_status_t OutgoingDirectory::Serve(::zx::channel request_directory) {
  return vfs_.ServeDirectory(root_, std::move(request_directory));
}

zx_status_t OutgoingDirectory::ServeFromStartupInfo() {
  return Serve(::zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));
}

zx_status_t OutgoingDirectory::AddNamedService(ServiceHandler handler, fit::string_view service,
                                               fit::string_view instance) const {
  return FindOrCreateDir(svc_, service)->AddEntry(instance, handler.TakeDirectory());
}

zx_status_t OutgoingDirectory::RemoveNamedService(fit::string_view service,
                                                  fit::string_view instance) const {
  fbl::RefPtr<fs::PseudoDir> service_dir = FindDir(svc_, service);
  if (service_dir == nullptr) {
    return ZX_ERR_IO_INVALID;
  }
  return service_dir->RemoveEntry(instance);
}

}  // namespace llcpp::sys
