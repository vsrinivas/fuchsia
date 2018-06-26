// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_STORAGE_STORY_STORAGE_XDR_H_
#define PERIDOT_BIN_USER_RUNNER_STORAGE_STORY_STORAGE_XDR_H_

#include <fuchsia/modular/internal/cpp/fidl.h>

#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

extern XdrFilterType<fuchsia::modular::ModuleData> XdrModuleData[];

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_STORAGE_STORY_STORAGE_XDR_H_
