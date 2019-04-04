// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_UTIL_GLM_WORKAROUND_H_
#define GARNET_LIB_UI_UTIL_GLM_WORKAROUND_H_

// TODO(SCN-666): Delete this file once ZX-377 is complete.
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

#endif  // GARNET_LIB_UI_UTIL_GLM_WORKAROUND_H_