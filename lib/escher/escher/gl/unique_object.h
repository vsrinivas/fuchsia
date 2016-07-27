// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if defined(ESCHER_USE_VULKAN_API)
#error not implemented
#elif defined(ESCHER_USE_METAL_API)
#error not implemented
#else
#include "escher/gl/gles2/unique_object.h"
namespace escher { using escher::gles2::UniqueObject; }
#endif
