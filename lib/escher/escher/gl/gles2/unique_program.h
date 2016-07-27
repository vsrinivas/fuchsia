// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "escher/gl/gles2/unique_object.h"
#include "escher/gl/gles2/unique_shader.h"

namespace escher {
namespace gles2 {

typedef UniqueObject<glDeleteProgram> UniqueProgram;
UniqueProgram MakeUniqueProgram(UniqueShader vertex_shader,
                                UniqueShader fragment_shader);
UniqueProgram MakeUniqueProgram(const std::string& vertex_shader_source,
                                const std::string& fragment_shader_source);

}  // namespace gles2
}  // namespace escher
