// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/view_identity.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

namespace scenic {

fuchsia::ui::views::ViewIdentityOnCreation NewViewIdentityOnCreation() {
  auto view_ref_pair = ViewRefPair::New();
  return {.view_ref = std::move(view_ref_pair.view_ref),
          .view_ref_control = std::move(view_ref_pair.control_ref)};
}

}  // namespace scenic
