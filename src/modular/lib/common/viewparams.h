// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_COMMON_VIEWPARAMS_H_
#define SRC_MODULAR_LIB_COMMON_VIEWPARAMS_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <variant>

namespace modular {

// Parameters for creating a Gfx view.
struct GfxViewParams {
  fuchsia::ui::views::ViewToken view_token;
  scenic::ViewRefPair view_ref_pair;
};

// Parameters for creating a Gfx or Flatland view.
using ViewParams = std::variant<GfxViewParams, fuchsia::ui::views::ViewCreationToken>;

}  // namespace modular

#endif  // SRC_MODULAR_LIB_COMMON_VIEWPARAMS_H_
