// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_UTIL_PRINT_OP_H_
#define GARNET_BIN_UI_SCENE_MANAGER_UTIL_PRINT_OP_H_

#include "lib/ui/scenic/fidl/ops.fidl.h"
#include "lib/ui/scenic/fidl/types.fidl.h"

#include <ostream>

namespace scene_manager {

std::ostream& operator<<(std::ostream& stream, const scenic::OpPtr& op);
std::ostream& operator<<(std::ostream& stream,
                         const scenic::CreateResourceOpPtr& op);
std::ostream& operator<<(std::ostream& stream,
                         const scenic::SetRendererParamOpPtr& op);
std::ostream& operator<<(std::ostream& stream, const scenic::Value::Tag& tag);

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_UTIL_PRINT_OP_H_
