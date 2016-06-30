// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if defined(__APPLE__)
#include "TargetConditionals.h"
#endif  // __APPLE__

#if defined(__APPLE__) && TARGET_OS_IPHONE

#include <OpenGLES/ES2/glext.h>

#else  // TARGET_OS_IPHONE

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#endif  // TARGET_OS_IPHONE
