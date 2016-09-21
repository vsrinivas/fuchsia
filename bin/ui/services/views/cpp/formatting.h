// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_UI_VIEWS_CPP_FORMATTING_H_
#define MOJO_SERVICES_UI_VIEWS_CPP_FORMATTING_H_

#include <iosfwd>

#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/services/views/interfaces/view_associates.mojom.h"
#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "mojo/public/cpp/bindings/formatting.h"
#include "mojo/services/geometry/cpp/formatting.h"

namespace mojo {
namespace ui {

std::ostream& operator<<(std::ostream& os, const mojo::ui::ViewToken& value);

std::ostream& operator<<(std::ostream& os,
                         const mojo::ui::ViewTreeToken& value);

std::ostream& operator<<(std::ostream& os, const mojo::ui::ViewInfo& value);

std::ostream& operator<<(std::ostream& os,
                         const mojo::ui::ViewProperties& value);
std::ostream& operator<<(std::ostream& os,
                         const mojo::ui::DisplayMetrics& value);
std::ostream& operator<<(std::ostream& os, const mojo::ui::ViewLayout& value);

std::ostream& operator<<(std::ostream& os,
                         const mojo::ui::ViewInvalidation& value);

std::ostream& operator<<(std::ostream& os,
                         const mojo::ui::ViewAssociateInfo& value);

}  // namespace ui
}  // namespace mojo

#endif  // MOJO_SERVICES_UI_VIEWS_CPP_FORMATTING_H_
