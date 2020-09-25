// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_GLM_WORKAROUND_GLM_WORKAROUND_H_
#define SRC_UI_LIB_GLM_WORKAROUND_GLM_WORKAROUND_H_

// TODO(fxbug.dev/23891): Delete this file once fxbug.dev/30337 is complete.
#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif

#endif  // SRC_UI_LIB_GLM_WORKAROUND_GLM_WORKAROUND_H_
