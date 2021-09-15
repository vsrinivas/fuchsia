// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/mouse_source.h"

#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl::input {

MouseSource::MouseSource(fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource> mouse_source,
                         fit::function<void()> error_handler)
    : MouseSourceBase(utils::ExtractKoid(mouse_source.channel()), /*close_channel=*/
                      [this](zx_status_t epitaph) {
                        binding_.Close(epitaph);
                        error_handler_();
                      }),
      binding_(this, std::move(mouse_source)),
      error_handler_(std::move(error_handler)) {
  binding_.set_error_handler([this](zx_status_t) { error_handler_(); });
}

}  // namespace scenic_impl::input
