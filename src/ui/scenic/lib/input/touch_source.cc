// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/touch_source.h"

#include <lib/async/default.h>

namespace scenic_impl::input {

TouchSource::TouchSource(zx_koid_t view_ref_koid,
                         fidl::ServerEnd<fuchsia_ui_pointer::TouchSource> touch_source,
                         fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond,
                         fit::function<void()> error_handler, GestureContenderInspector& inspector)
    : TouchSourceBase(
          utils::ExtractKoid(touch_source.channel()), view_ref_koid, std::move(respond),
          [this](zx_status_t epitaph) { CloseChannel(epitaph); },
          /*augment*/ [](auto&...) {}, inspector),
      binding_(async_get_default_dispatcher(), std::move(touch_source), this,
               [error_handler = std::move(error_handler)](fidl::UnbindInfo) {
                 // NOTE: Triggers destruction of this object.
                 error_handler();
               }) {}

void TouchSource::CloseChannel(zx_status_t epitaph) { binding_.Close(epitaph); }

}  // namespace scenic_impl::input
