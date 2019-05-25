// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GARNET_LIB_UI_GFX_UTIL_VALIDATE_EVENTPAIR_H_
#define GARNET_LIB_UI_GFX_UTIL_VALIDATE_EVENTPAIR_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/zx/eventpair.h>
#include <zircon/rights.h>

namespace scenic_impl {
namespace gfx {

// True IFF eventpairs are valid, are peers, and have expected rights.
bool validate_eventpair(const zx::eventpair& a_object, zx_rights_t a_rights,
                        const zx::eventpair& b_object, zx_rights_t b_rights);

// True IFF ViewRefControl and ViewRef are valid, are peers, and have expected
// rights.
//  - The control ref is expected to have ZX_DEFAULT_EVENTPAIR_RIGHTS.
//  - The view ref is expected to have ZX_RIGHTS_BASIC.
bool validate_viewref(const fuchsia::ui::views::ViewRefControl& control_ref,
                      const fuchsia::ui::views::ViewRef& view_ref);

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_UTIL_VALIDATE_EVENTPAIR_H_
