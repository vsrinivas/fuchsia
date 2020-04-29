// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_SYSTEM_DIAGNOSTICS_DIRECTORY_H_
#define SRC_SYS_APPMGR_SYSTEM_DIAGNOSTICS_DIRECTORY_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/process.h>

namespace component {

class SystemDiagnosticsDirectory {
 public:
  explicit SystemDiagnosticsDirectory(zx::process process);

  const inspect::Inspector& inspector() const { return inspector_; }

 private:
  zx::process process_;
  inspect::Inspector inspector_;
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_SYSTEM_DIAGNOSTICS_DIRECTORY_H_
