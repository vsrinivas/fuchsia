// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/gfx_magnifier_delegate.h"

namespace a11y {

void GfxMagnifierDelegate::RegisterHandler(
    fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) {
  handler_scope_.Reset();
  handler_ = handler.Bind();
}

void GfxMagnifierDelegate::SetMagnificationTransform(float scale, float x, float y,
                                                     SetMagnificationTransformCallback callback) {
  if (!handler_) {
    return;
  }

  handler_->SetClipSpaceTransform(
      x, y, scale, handler_scope_.MakeScoped([callback = std::move(callback)] { callback(); }));
}

}  // namespace a11y
