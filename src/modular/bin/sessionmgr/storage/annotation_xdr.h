// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_ANNOTATION_XDR_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_ANNOTATION_XDR_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "src/modular/lib/fidl/json_xdr.h"

namespace modular {

void XdrAnnotation(XdrContext* const xdr, fuchsia::modular::Annotation* const data);

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_ANNOTATION_XDR_H_
