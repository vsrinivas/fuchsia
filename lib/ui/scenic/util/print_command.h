// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_UTIL_PRINT_COMMAND_H_
#define GARNET_LIB_UI_SCENIC_UTIL_PRINT_COMMAND_H_

#include <gfx/cpp/fidl.h>

#include <ostream>

std::ostream& operator<<(std::ostream& stream, const gfx::Command& command);
std::ostream& operator<<(std::ostream& stream,
                         const gfx::CreateResourceCommand& command);
std::ostream& operator<<(std::ostream& stream,
                         const gfx::SetRendererParamCommand& command);
std::ostream& operator<<(std::ostream& stream, const gfx::Value::Tag& tag);

#endif  // GARNET_LIB_UI_SCENIC_UTIL_PRINT_COMMAND_H_
