// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_FIDL_PRINT_H_
#define LIB_UI_SCENIC_CPP_FIDL_PRINT_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <ostream>

// TODO(SCN-806): support printing of more types.
std::ostream& operator<<(std::ostream& str, const fuchsia::ui::gfx::vec2& vec);
std::ostream& operator<<(std::ostream& str, const fuchsia::ui::gfx::vec3& vec);
std::ostream& operator<<(std::ostream& str, const fuchsia::ui::gfx::vec4& vec);
std::ostream& operator<<(std::ostream& str,
                         const fuchsia::ui::gfx::BoundingBox& box);
std::ostream& operator<<(std::ostream& str,
                         const fuchsia::ui::gfx::ViewProperties& props);

#endif  // LIB_UI_SCENIC_CPP_FIDL_PRINT_H_
