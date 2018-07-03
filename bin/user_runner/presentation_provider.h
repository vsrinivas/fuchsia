// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PRESENTATION_PROVIDER_H_
#define PERIDOT_BIN_USER_RUNNER_PRESENTATION_PROVIDER_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/macros.h>

namespace modular {

// Interface that allows a class that implements this functionality to pass a
// purposeful reference to itself to another class that needs the functionality.
class PresentationProvider {
 public:
  PresentationProvider();
  virtual ~PresentationProvider();

  virtual void GetPresentation(
      fidl::StringPtr story_id,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) = 0;

  virtual void WatchVisualState(
      fidl::StringPtr story_id,
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher>
          watcher) = 0;
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PRESENTATION_PROVIDER_H_
