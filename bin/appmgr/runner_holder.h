// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_RUNNER_HOLDER_H_
#define GARNET_BIN_APPMGR_RUNNER_HOLDER_H_

#include <lib/zx/vmo.h>

#include <component/cpp/fidl.h>
#include "garnet/bin/appmgr/namespace.h"
#include "garnet/lib/farfs/file_system.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/svc/cpp/services.h"

namespace component {

class RunnerHolder {
 public:
  RunnerHolder(Services services, ComponentControllerPtr controller);
  ~RunnerHolder();

  void StartComponent(Package package, StartupInfo startup_info,
                      std::unique_ptr<archive::FileSystem> file_system,
                      fxl::RefPtr<Namespace> ns,
                      fidl::InterfaceRequest<ComponentController> controller);

 private:
  Services services_;
  ComponentControllerPtr controller_;
  RunnerPtr runner_;

  // TODO(abarth): We hold these objects for the lifetime of the runner, but we
  // should actuall drop them once their controller is done.
  std::vector<std::unique_ptr<archive::FileSystem>> file_systems_;
  std::vector<fxl::RefPtr<Namespace>> namespaces_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RunnerHolder);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_RUNNER_HOLDER_H_
