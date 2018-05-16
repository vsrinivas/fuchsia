// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <presentation/cpp/fidl.h>
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"

namespace modular {

// Interface that allows a class that implements this functionality to pass a
// purposeful reference to itself to another class that needs the functionality.
class PresentationProvider {
 public:
  PresentationProvider();
  virtual ~PresentationProvider();

  virtual void GetPresentation(
      fidl::StringPtr story_id,
      fidl::InterfaceRequest<presentation::Presentation> request) = 0;
};

}  // namespace modular
