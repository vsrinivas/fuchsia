// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/gles2/unique_program.h"

#include "escher/gl/gles2/bindings.h"

#include <iostream>
#include <vector>

namespace escher {
namespace gles2 {

UniqueProgram MakeUniqueProgram(UniqueShader vertex_shader,
                                UniqueShader fragment_shader) {
  UniqueProgram program;
  program.Reset(glCreateProgram());
  glAttachShader(program.id(), vertex_shader.id());
  glAttachShader(program.id(), fragment_shader.id());
  glLinkProgram(program.id());

  GLint link_status;
  glGetProgramiv(program.id(), GL_LINK_STATUS, &link_status);
  if (link_status)
    return program;

  GLint info_log_length;
  glGetProgramiv(program.id(), GL_INFO_LOG_LENGTH, &info_log_length);
  if (info_log_length > 1) {
    std::vector<GLchar> info_log(info_log_length);
    glGetProgramInfoLog(program.id(), static_cast<GLsizei>(info_log.size()),
                        nullptr, info_log.data());
    // TODO(abarth): Switch to a reasonable logging system.
    std::cerr << "program link failed: " << info_log.data();
  } else {
    std::cerr << "program link failed. <Empty log message>";
  }
  return UniqueProgram();
}

UniqueProgram MakeUniqueProgram(const std::string& vertex_shader_source,
                                const std::string& fragment_shader_source) {
  UniqueShader vertex_shader =
      MakeUniqueShader(GL_VERTEX_SHADER, vertex_shader_source);
  UniqueShader fragment_shader =
      MakeUniqueShader(GL_FRAGMENT_SHADER, fragment_shader_source);
  if (!vertex_shader || !fragment_shader)
    return UniqueProgram();
  return MakeUniqueProgram(std::move(vertex_shader),
                           std::move(fragment_shader));
}

}  // namespace gles2
}  // namespace escher
