// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/gles2/unique_shader.h"

#include <iostream>

namespace escher {
namespace gles2 {

namespace {
UniqueShader CompileShader(GLenum type,
                           GLsizei count,
                           const GLchar** contents,
                           const GLint* lengths) {
  UniqueShader shader;
  shader.Reset(glCreateShader(type));
  glShaderSource(shader.id(), count, contents, lengths);
  glCompileShader(shader.id());

  GLint compile_status;
  glGetShaderiv(shader.id(), GL_COMPILE_STATUS, &compile_status);
  if (compile_status)
    return shader;

#ifndef NDEBUG
  GLint info_log_length;
  glGetShaderiv(shader.id(), GL_INFO_LOG_LENGTH, &info_log_length);
  if (info_log_length > 1) {
    std::vector<GLchar> info_log(info_log_length);
    glGetShaderInfoLog(shader.id(), static_cast<GLsizei>(info_log.size()),
                       nullptr, info_log.data());
    // TODO(abarth): Switch to a reasonable logging system.
    std::cerr << "shader compilation failed: " << info_log.data();
  } else {
    std::cerr << "shader compilation failed. <Empty log message>";
  }
#endif
  return UniqueShader();
}
}  // namespace

UniqueShader MakeUniqueShader(GLenum type, const std::string& source) {
  const GLchar* contents[1] = {source.c_str()};
  GLint lengths[1] = {static_cast<GLint>(source.size())};
  return CompileShader(type, 1, contents, lengths);
}

UniqueShader MakeUniqueShader(GLenum type,
                              const std::vector<std::string>& sources) {
  const GLchar* contents[sources.size()];
  GLint lengths[sources.size()];
  for (size_t i = 0; i < sources.size(); i++) {
    contents[i] = sources[i].c_str();
    lengths[i] = static_cast<GLint>(sources[i].size());
  }
  return CompileShader(type, static_cast<GLsizei>(sources.size()), contents,
                       lengths);
}

}  // namespace gles2
}  // namespace escher
