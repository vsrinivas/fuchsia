// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_UTIL_PRINT_COMMAND_H_
#define GARNET_LIB_UI_GFX_UTIL_PRINT_COMMAND_H_

#include "lib/ui/gfx/fidl/commands.fidl.h"
#include "lib/ui/gfx/fidl/types.fidl.h"

#include <ostream>

std::ostream& operator<<(std::ostream& stream,
                         const ui::gfx::CommandPtr& command);
std::ostream& operator<<(std::ostream& stream,
                         const ui::gfx::CreateResourceCommandPtr& command);
std::ostream& operator<<(std::ostream& stream,
                         const ui::gfx::SetRendererParamCommandPtr& command);
std::ostream& operator<<(std::ostream& stream, const ui::gfx::Value::Tag& tag);

#endif  // GARNET_LIB_UI_GFX_UTIL_PRINT_COMMAND_H_
