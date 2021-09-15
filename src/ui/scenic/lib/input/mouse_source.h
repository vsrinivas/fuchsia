// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_MOUSE_SOURCE_H_
#define SRC_UI_SCENIC_LIB_INPUT_MOUSE_SOURCE_H_

#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include "src/ui/scenic/lib/input/mouse_source_base.h"

namespace scenic_impl::input {

// Implementation of the |fuchsia::ui::pointer::MouseSource| interface. One instance per
// channel.
class MouseSource : public MouseSourceBase, fuchsia::ui::pointer::MouseSource {
 public:
  MouseSource(fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource> event_provider,
              fit::function<void()> error_handler);

  ~MouseSource() override = default;

  // |fuchsia::ui::pointer::MouseSource|
  void Watch(WatchCallback callback) override { MouseSourceBase::WatchBase(std::move(callback)); }

 private:
  fidl::Binding<fuchsia::ui::pointer::MouseSource> binding_;
  const fit::function<void()> error_handler_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_MOUSE_SOURCE_H_
