// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "escher/gl/unique_object.h"

namespace escher {

typedef UniqueObject<glDeleteShader> UniqueShader;
UniqueShader MakeUniqueShader(GLenum type, const std::string& source);
UniqueShader MakeUniqueShader(GLenum type,
                              const std::vector<std::string>& sources);

}  // namespace escher
