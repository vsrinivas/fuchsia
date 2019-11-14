// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/escher_process_init.h"

#if ESCHER_USE_RUNTIME_GLSL
#include "glslang/Public/ShaderLang.h"

namespace escher {

void GlslangInitializeProcess() { glslang::InitializeProcess(); }

void GlslangFinalizeProcess() { glslang::FinalizeProcess(); }

}  // namespace escher
#endif  // ESCHER_USE_RUNTIME_GLSL
