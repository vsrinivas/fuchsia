// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_UTIL_PRINT_OP_H_
#define GARNET_LIB_UI_GFX_UTIL_PRINT_OP_H_

#include "lib/ui/gfx/fidl/ops.fidl.h"
#include "lib/ui/gfx/fidl/types.fidl.h"

#include <ostream>

std::ostream& operator<<(std::ostream& stream, const scenic::OpPtr& op);
std::ostream& operator<<(std::ostream& stream,
                         const scenic::CreateResourceOpPtr& op);
std::ostream& operator<<(std::ostream& stream,
                         const scenic::SetRendererParamOpPtr& op);
std::ostream& operator<<(std::ostream& stream, const scenic::Value::Tag& tag);

#endif  // GARNET_LIB_UI_GFX_UTIL_PRINT_OP_H_
