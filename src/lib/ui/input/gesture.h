// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_UI_INPUT_GESTURE_H_
#define SRC_LIB_UI_INPUT_GESTURE_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include <map>

namespace input {

// Tracks an individual gesture, defined as a set of touch points in an
// interaction with a particular input device. Changes are transformed into a
// |Gesture::Delta| with aggregate 2D relative translation, rotation, and scale
// as suggested by touch point updates.
//
// This class does not handle tapping or threshold-based classification of
// gestures, which can be handled by the caller.
class Gesture {
 public:
  using PointerId = uint32_t;

  // Represents a change from the previous |UpdatePointer| state.
  struct Delta {
    Delta& operator+=(const Delta& other);
    bool operator==(const Delta& other) const;

    // Delta translation is expressed in the coordinate system determined by
    // the input source. For most use cases, this is the view space, which is
    // x-right y-down and scaled according to view properties.
    fuchsia::ui::gfx::vec2 translation;
    // Delta rotation is expressed in radians about the right-hand z of the
    // coordinate system of the input source. For most use cases, this is
    // clockwise.
    float rotation = 0;
    float scale = 1;
  };

  bool has_pointers() const { return !pointers_.empty(); }
  size_t pointer_count() const { return pointers_.size(); }
  const fuchsia::ui::gfx::vec2& centroid() const { return centroid_; }

  void AddPointer(PointerId pointer_id, const fuchsia::ui::gfx::vec2& position);
  Delta UpdatePointer(PointerId pointer_id,
                      const fuchsia::ui::gfx::vec2& position);
  void RemovePointer(PointerId pointer_id);

 private:
  struct PointerInfo {
    fuchsia::ui::gfx::vec2 absolute;  // input coordinates
    fuchsia::ui::gfx::vec2 relative;  // centroid coordinates
    float distance;                   // centroid-relative
  };

  void UpdateCentroid();
  void UpdateRelative();

  fuchsia::ui::gfx::vec2 centroid_;
  std::map<PointerId, PointerInfo> pointers_;
};

}  // namespace input

#endif  // SRC_LIB_UI_INPUT_GESTURE_H_
