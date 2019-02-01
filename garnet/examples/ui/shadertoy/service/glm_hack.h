// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_GLM_HACK_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_GLM_HACK_H_

// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
//
// Tracked by MG-377.

#if defined(countof)
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
#include <glm/glm.hpp>
#endif

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_GLM_HACK_H_
