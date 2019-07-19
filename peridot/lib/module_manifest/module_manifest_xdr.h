// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_MODULE_MANIFEST_XDR_H_
#define PERIDOT_LIB_MODULE_MANIFEST_MODULE_MANIFEST_XDR_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

extern const XdrFilterType<fuchsia::modular::ModuleManifest> XdrModuleManifest[];

void XdrModuleManifest_v1(XdrContext* const xdr, fuchsia::modular::ModuleManifest* const data);

void XdrModuleManifest_v2(XdrContext* const xdr, fuchsia::modular::ModuleManifest* const data);

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_MODULE_MANIFEST_XDR_H_
