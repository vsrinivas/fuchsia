// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_COLOR_CONVERSION_STATE_MACHINE_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_COLOR_CONVERSION_STATE_MACHINE_H_

#include <array>
#include <optional>

namespace flatland {

extern const std::array<float, 9> kDefaultColorConversionCoefficients;
extern const std::array<float, 3> kDefaultColorConversionOffsets;

// Color Conversion Data Struct. The data here modulates pixel data with
// the following formula:
//     [coefficients * (pixel + preoffsets) + postoffsets].
// where 'pixel' is comprised of the RGB components of the physical pixel
// on the display.
struct ColorConversionData {
  std::array<float, 9> coefficients = kDefaultColorConversionCoefficients;
  std::array<float, 3> preoffsets = kDefaultColorConversionOffsets;
  std::array<float, 3> postoffsets = kDefaultColorConversionOffsets;

  bool operator==(const ColorConversionData& rhs) const {
    return this->coefficients == rhs.coefficients && this->preoffsets == rhs.preoffsets &&
           this->postoffsets == rhs.postoffsets;
  }

  bool operator!=(const ColorConversionData& rhs) const { return !(*this == rhs); }
};

// Due to semantic differences between how the display controller (DC) and the GPU renderer
// handle color conversion (CC), the logic of when and how to apply color correction is
// surprisingly complex. This class is meant to encapsulate that logic separately from
// the display compositor, engine, and other graphics code.
//
// On the one hand, the DC is stateful. This means that once CC is set and confirmed with
// a successful call to ApplyConfig(), it continues to apply on all subsequent frames until
// new CC values are set and confirmed with a new call to ApplyConfig().
//
// On the other hand, the GPU renderer is not stateful. It needs to be told every frame whether
// or not it should apply color correction.
//
// This can lead to some undesirable scenarios if not properly handled. For instance, say on
// frame N we use the DC for CC, and on frame N+1 we need to switch to GPU rendering. The CC
// that was applied to the DC on frame N is still in effect. This means that if the GPU
// renderer were to apply color correction on frame N+1, we would in effect be applying CC twice.
class ColorConversionStateMachine {
 public:
  void SetData(const ColorConversionData& data);

  // Returns the CC data that should be applied next. If std::nullopt, then there is no data that
  // needs applying at the current time.
  std::optional<ColorConversionData> GetDataToApply();

  // Should be called directly after a successful call DisplayController::ApplyConfig() with
  // valid color correction data.
  void SetApplyConfigSucceeded();

  // There are times where the GPU rendering path will need to clear past color conversion state
  // from the display controller before applying its own state. This happens if State_A is applied
  // on Frame_A on the display controller, but then the client updates the color conversion state,
  // but the new state is unable to be applied to the display controller and we need to fallback
  // to GPU composition. If we do not clear the old state, we will end up applying the new state
  // on top of the old one, to undefined results.
  bool GpuRequiresDisplayClearing();

  // Call this after clearing the display state on the GPU path when prompted to do so by
  // |GpuRequiresDisplayClearing|=true.
  void DisplayCleared();

 private:
  // Represents if there is some color correction state that has been applied successfully to
  // the display controller.
  bool dc_has_cc_ = false;

  // The latest CC data to be provided by the client.
  ColorConversionData data_;

  // The data that was applied at the time of the last successful ApplyConfig();
  ColorConversionData applied_data_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_COLOR_CONVERSION_STATE_MACHINE_H_
