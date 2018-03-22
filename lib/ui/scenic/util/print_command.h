// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/gfx.h>

#include <ostream>

std::ostream& operator<<(std::ostream& stream, const gfx::Command& command);
std::ostream& operator<<(std::ostream& stream,
                         const gfx::CreateResourceCommand& command);
std::ostream& operator<<(std::ostream& stream,
                         const gfx::SetRendererParamCommand& command);
std::ostream& operator<<(std::ostream& stream, const gfx::Value::Tag& tag);
