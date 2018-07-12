// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_APPMGR_H_
#define GARNET_BIN_APPMGR_APPMGR_H_

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>

#include "garnet/bin/appmgr/realm.h"
#include "garnet/bin/appmgr/root_loader.h"
#include "garnet/bin/appmgr/util.h"
#include "lib/fxl/macros.h"

namespace component {

struct AppmgrArgs {
  zx_handle_t pa_directory_request;
  std::string sysmgr_url;
  fidl::VectorPtr<fidl::StringPtr> sysmgr_args;
  bool run_virtual_console;
  bool retry_sysmgr_crash;
};

class Appmgr {
 public:
  Appmgr(async_dispatcher_t* dispatcher, AppmgrArgs args);
  ~Appmgr();

 private:
  fs::SynchronousVfs loader_vfs_;
  RootLoader root_loader_;
  fbl::RefPtr<fs::PseudoDir> loader_dir_;

  std::unique_ptr<Realm> root_realm_;
  fs::SynchronousVfs publish_vfs_;
  fbl::RefPtr<fs::PseudoDir> publish_dir_;

  fuchsia::sys::ComponentControllerPtr sysmgr_;
  std::string sysmgr_url_;
  fidl::VectorPtr<fidl::StringPtr> sysmgr_args_;
  RestartBackOff sysmgr_backoff_;
  bool sysmgr_permanently_failed_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Appmgr);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_APPMGR_H_
