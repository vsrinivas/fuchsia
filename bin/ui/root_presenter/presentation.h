// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>

#include "garnet/bin/ui/root_presenter/displays/display_model.h"
#include "lib/ui/input/input_device_impl.h"
#include "lib/ui/scenic/cpp/resources.h"

namespace root_presenter {

// Base class for Presentation. Exposes only what is needed by
// |root_presenter::App|.
class Presentation : protected fuchsia::ui::policy::Presentation {
 public:
  virtual ~Presentation() {}

  // Callback when the presentation yields to the next/previous one.
  using YieldCallback = fit::function<void(bool yield_to_next)>;
  // Callback when the presentation is shut down.
  using ShutdownCallback = fit::closure;

  virtual const scenic::Layer& layer() const = 0;
  virtual const YieldCallback& yield_callback() = 0;

  virtual void OnReport(uint32_t device_id,
                        fuchsia::ui::input::InputReport report) = 0;
  virtual void OnDeviceAdded(mozart::InputDeviceImpl* input_device) = 0;
  virtual void OnDeviceRemoved(uint32_t device_id) = 0;

 protected:
  virtual float display_rotation_desired() const = 0;
  virtual void set_display_rotation_desired(float display_rotation) = 0;
  virtual float display_rotation_current() const = 0;
  virtual const DisplayModel::DisplayInfo& display_info() = 0;

  virtual const DisplayMetrics& display_metrics() const = 0;

  virtual scenic::Camera* camera() = 0;

  virtual void SetDisplayUsageWithoutApplyingChanges(
      fuchsia::ui::policy::DisplayUsage usage_) = 0;

  // Returns false if the operation failed (e.g. the requested size is bigger
  // than the actual display size).
  virtual bool SetDisplaySizeInMmWithoutApplyingChanges(float width_in_mm,
                                                        float height_in_mm,
                                                        bool print_errors) = 0;

  // Sets |display_metrics_| and updates Scenic.
  // If |present_changes| is true, the changes will be presented on the existing
  // Session; otherwise, the caller will have to do that themselves.
  //
  // Returns false if the updates were skipped (if display initialization hasn't
  // happened yet).
  virtual bool ApplyDisplayModelChanges(bool print_log,
                                        bool present_changes) = 0;

  friend class DisplayRotater;
  friend class DisplayUsageSwitcher;
  friend class PerspectiveDemoMode;
  friend class DisplaySizeSwitcher;
  friend class PresentationSwitcher;
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_
