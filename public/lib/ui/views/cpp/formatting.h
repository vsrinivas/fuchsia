// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_VIEWS_CPP_FORMATTING_H_
#define LIB_UI_VIEWS_CPP_FORMATTING_H_

#include <iosfwd>

#include "lib/fidl/cpp/bindings/formatting.h"
#include "lib/ui/geometry/cpp/formatting.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"

namespace mozart {

std::ostream& operator<<(std::ostream& os, const ViewToken& value);

std::ostream& operator<<(std::ostream& os, const ViewTreeToken& value);

std::ostream& operator<<(std::ostream& os, const ViewInfo& value);

std::ostream& operator<<(std::ostream& os, const ViewProperties& value);
std::ostream& operator<<(std::ostream& os, const ViewLayout& value);

}  // namespace mozart

#endif  // LIB_UI_VIEWS_CPP_FORMATTING_H_
