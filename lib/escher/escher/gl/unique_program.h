// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "escher/gl/unique_object.h"
#include "escher/gl/unique_shader.h"

namespace escher {

typedef UniqueObject<glDeleteProgram> UniqueProgram;
UniqueProgram MakeUniqueProgram(UniqueShader vertex_shader,
                                UniqueShader fragment_shader);
UniqueProgram MakeUniqueProgram(const std::string& vertex_shader_source,
                                const std::string& fragment_shader_source);

}  // namespace escher
