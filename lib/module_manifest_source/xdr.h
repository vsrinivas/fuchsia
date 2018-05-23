// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_SOURCE_XDR_H_
#define PERIDOT_LIB_MODULE_MANIFEST_SOURCE_XDR_H_

#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

class ModuleManifest;

extern const XdrFilterType<ModuleManifest> XdrModuleManifest[];

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_SOURCE_XDR_H_
