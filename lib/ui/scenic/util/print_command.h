// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_UTIL_PRINT_COMMAND_H_
#define GARNET_LIB_UI_SCENIC_UTIL_PRINT_COMMAND_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include <ostream>

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::Command& command);
std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::CreateResourceCmd& command);
std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::SetRendererParamCmd& command);
std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::Value::Tag& tag);

#endif  // GARNET_LIB_UI_SCENIC_UTIL_PRINT_COMMAND_H_
