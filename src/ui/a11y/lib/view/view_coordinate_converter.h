// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_VIEW_COORDINATE_CONVERTER_H_
#define SRC_UI_A11Y_LIB_VIEW_VIEW_COORDINATE_CONVERTER_H_

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/observation/geometry/cpp/fidl.h>
#include <fuchsia/ui/observation/scope/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace a11y {

// A helper class to convert between Scenic View coordinate spaces.
class ViewCoordinateConverter {
 public:
  // Callback that can be registered to be called whenever there is a change in view geometry.
  using OnGeometryChangeCallback = fit::function<void()>;

  // |context_view_ref_koid| serves as the context view when registering a new geometry observer.
  // Please check fuchsia.ui.observation.scope.Registry for full details.
  ViewCoordinateConverter(fuchsia::ui::observation::scope::RegistryPtr registry,
                          zx_koid_t context_view_ref_koid);

  virtual ~ViewCoordinateConverter() = default;

  // Converts a |coordinate| in |view_ref_koid| space into |context_view_ref_koid_| space. Returns
  // nullptr if |view_ref_koid| is not a known child of |context_view_ref_koid_|.
  virtual std::optional<fuchsia::math::PointF> Convert(zx_koid_t view_ref_koid,
                                                       fuchsia::math::PointF coordinate) const;

  // Registers a callback that is invoked whenever there are changes in view geometry.
  virtual void RegisterCallback(OnGeometryChangeCallback callback);

 private:
  // Space data about a particular view V in W. For this object, W is always
  // |context_view_ref_koid_|.
  struct ViewData {
    // The origin of view V in V coordinates.
    fuchsia::math::PointF origin;
    // The origin of view V in W coordinates.
    fuchsia::math::PointF origin_in_context;
    // The clockwise rotation about the origin of V, in degrees.
    float angle;
    // A scaling factor applied to the x-axis to convert from V coordinates to W.
    float x_scale;
    // A scaling factor applied to the y-axis to convert from V coordinates to W.
    float y_scale;
  };

  // Processes a response from the geometry observer, storing relevant view data used to perform
  // conversions.
  void ProcessResponse(fuchsia::ui::observation::geometry::WatchResponse response);

  // Helper method to watch for the next geometry watcher response.
  void Watch();

  // The context ViewRef's koid observing geometry changes.
  zx_koid_t context_view_ref_koid_;

  // Data used to convert from a  view V into the context view's space.
  std::unordered_map<zx_koid_t, ViewData> view_transforms_;

  fuchsia::ui::observation::geometry::ViewTreeWatcherPtr watcher_;

  std::vector<OnGeometryChangeCallback> callbacks_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_COORDINATE_CONVERTER_H_
