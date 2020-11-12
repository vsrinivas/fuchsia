// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLETS_LOADER_H_
#define SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLETS_LOADER_H_

#include <zircon/types.h>

#include "src/connectivity/weave/lib/applets/weave_applets.h"
#include "src/connectivity/weave/lib/applets_loader/applet.h"
#include "src/connectivity/weave/lib/applets_loader/applets_module.h"

namespace weavestack::applets {

class AppletsLoader {
 public:
  // Creates a applets loader by opening the loadable module specified by |lib_name|.
  // Returns ZX_ERR_UNAVAILABLE if the shared library could not be opened.
  static zx_status_t CreateWithModule(const char* lib_name, std::unique_ptr<AppletsLoader>* out);

  // Creates a 'null' applets loader, which cannot create an applet.
  static std::unique_ptr<AppletsLoader> CreateWithNullModule();

  // Creates an applet with the given callback.
  Applet CreateApplet(FuchsiaWeaveAppletsCallbacksV1 callbacks);

 private:
  explicit AppletsLoader(AppletsModuleV1 module);

  AppletsModuleV1 module_;
};

}  // namespace weavestack::applets

#endif  // SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLETS_LOADER_H_
