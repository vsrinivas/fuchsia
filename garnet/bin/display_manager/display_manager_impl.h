// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DISPLAY_MANAGER_DISPLAY_MANAGER_IMPL_H_
#define GARNET_BIN_DISPLAY_MANAGER_DISPLAY_MANAGER_IMPL_H_

#include <fuchsia/device/display/cpp/fidl.h>

#include "display.h"
#include <lib/sys/cpp/component_context.h>
#include <lib/fidl/cpp/binding_set.h>

namespace display {

// This class is a thin wrapper around a Display object, implementing
// the DisplayManager FIDL interface.
class DisplayManagerImpl : public fuchsia::device::display::Manager {
 public:
  DisplayManagerImpl();
  virtual void GetBrightness(GetBrightnessCallback callback);
  virtual void SetBrightness(double brightness, SetBrightnessCallback callback);

 protected:
  DisplayManagerImpl(std::unique_ptr<sys::ComponentContext> context);

 private:
  DisplayManagerImpl(const DisplayManagerImpl&) = delete;
  DisplayManagerImpl& operator=(const DisplayManagerImpl&) = delete;

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<Manager> bindings_;
  std::unique_ptr<Display> display_;
};
}  // namespace display

#endif  // GARNET_BIN_DISPLAY_MANAGER_DISPLAY_MANAGER_IMPL_H_
