// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/util/validate_eventpair.h"

namespace scenic_impl {
namespace gfx {

bool validate_eventpair(const zx::eventpair& a_object, zx_rights_t a_rights,
                        const zx::eventpair& b_object, zx_rights_t b_rights) {
  if (a_object.get_info(ZX_INFO_HANDLE_VALID, nullptr,
                        /*buffer size*/ 0, nullptr, nullptr) != ZX_OK) {
    return false;  // bad handle
  }

  if (b_object.get_info(ZX_INFO_HANDLE_VALID, nullptr,
                        /*buffer size*/ 0, nullptr, nullptr) != ZX_OK) {
    return false;  // bad handle
  }

  zx_info_handle_basic_t a_info{};
  if (a_object.get_info(ZX_INFO_HANDLE_BASIC, &a_info, sizeof(a_info), nullptr,
                        nullptr) != ZX_OK) {
    return false;  // no info
  }
  if (a_info.rights != a_rights) {
    return false;  // unexpected rights
  }

  zx_info_handle_basic_t b_info{};
  if (b_object.get_info(ZX_INFO_HANDLE_BASIC, &b_info, sizeof(b_info), nullptr,
                        nullptr) != ZX_OK) {
    return false;  // no info
  }
  if (b_info.rights != b_rights) {
    return false;  // unexpected rights
  }

  if (a_info.koid != b_info.related_koid) {
    return false;  // unrelated eventpair
  }

  return true;
}

bool validate_viewref(const fuchsia::ui::views::ViewRefControl& control_ref,
                      const fuchsia::ui::views::ViewRef& view_ref) {
  return validate_eventpair(control_ref.reference, ZX_DEFAULT_EVENTPAIR_RIGHTS,
                            view_ref.reference, ZX_RIGHTS_BASIC);
}

}  // namespace gfx
}  // namespace scenic_impl
