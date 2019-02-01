// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DISPLAY_MANAGER_DISPLAY_H_
#define GARNET_BIN_DISPLAY_MANAGER_DISPLAY_H_

#include <lib/zx/channel.h>
#include <memory>
#include "display.h"

namespace display {

// The Display class is responsible for exposing display control. It
// encapsulates interacting with the driver in the Zircon layer.
class Display {
 public:
  // Default constructor, taking the fidl interface for the display.
  Display(zx::channel channel);
  ~Display();

  // Instantiates an Display instance. For now, we only return the default
  // embedded display. If there is an error retrieving the display, NULL is
  // returned.
  static Display* GetDisplay();

  // Retrieves the backlight's current brightness. The brightness is set  as a
  // percentage of the max brightness. If successful, true will be returned.
  // False otherwise.
  bool GetBrightness(double* brightness);

  // Sets the backlight's brightness. The brightness is specified as a
  // percentage of the max brightness.
  bool SetBrightness(double brightness);

 private:
  zx::channel channel_;
};
}  // namespace display

#endif  // GARNET_BIN_DISPLAY_MANAGER_DISPLAY_H_
