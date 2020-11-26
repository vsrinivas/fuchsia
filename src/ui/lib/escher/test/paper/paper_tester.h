// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_PAPER_PAPER_TESTER_H_
#define SRC_UI_LIB_ESCHER_TEST_PAPER_PAPER_TESTER_H_

#include "src/ui/lib/escher/paper/paper_draw_call_factory.h"

namespace escher {

class PaperTester {
 public:
  static PaperShaderList GetShaderList(const PaperDrawCallFactory& factory, const Material& mat,
                                       bool cast_shadows) {
    return factory.GetShaderList(mat, cast_shadows);
  }

  static ShaderProgram* ambient_light_program(const PaperDrawCallFactory& factory) {
    return factory.ambient_light_program_.get();
  }
  static ShaderProgram* no_lighting_program(const PaperDrawCallFactory& factory) {
    return factory.no_lighting_program_.get();
  }
  static ShaderProgram* point_light_program(const PaperDrawCallFactory& factory) {
    return factory.point_light_program_.get();
  }
  static ShaderProgram* shadow_volume_geometry_program(const PaperDrawCallFactory& factory) {
    return factory.shadow_volume_geometry_program_.get();
  }
  static ShaderProgram* shadow_volume_geometry_debug_program(const PaperDrawCallFactory& factory) {
    return factory.shadow_volume_geometry_debug_program_.get();
  }
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TEST_PAPER_PAPER_TESTER_H_
