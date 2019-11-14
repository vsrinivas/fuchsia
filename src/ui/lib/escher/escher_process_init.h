// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_ESCHER_PROCESS_INIT_H_
#define SRC_UI_LIB_ESCHER_ESCHER_PROCESS_INIT_H_

namespace escher {

// Escher currently incorporates the Khronos GLSL reference compiler ("glslang")
// that is included in the Vulkan SDK.  This codebase requires that the client
// call glslang::InitializeProcess() before using any of its functionality, and
// glslang::FinalizeProcess() when finished.
//
// Some Escher clients may wish to directly use the glslang framework; in order
// to not conflict with these use cases, Escher requires clients to also call
// these two functions, before the first Escher instance is created, and after
// the last instance is destroyed.
//
// For clients that do not glslang framework, Escher provides the following
// two wrapper functions so that the client does not need to include the glslang
// headers.
//
// This is admittedly clumsy.  In the long term, Escher will avoid initializing
// any per-process or per-thread state that may conflict with other client code;
// it will be enough to simply create and destroy Escher instances.

#if ESCHER_USE_RUNTIME_GLSL
void GlslangInitializeProcess();
void GlslangFinalizeProcess();
#endif

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_ESCHER_PROCESS_INIT_H_
