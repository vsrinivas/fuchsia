// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TYPES_COLOR_HISTOGRAM_H_
#define SRC_UI_LIB_ESCHER_TYPES_COLOR_HISTOGRAM_H_

#include <map>
#include <ostream>

#include "src/ui/lib/escher/types/color.h"

namespace escher {

// Counts the frequencies of each color in an image.  Pixels of the image
// are assumed to be tightly packed (i.e. no row padding).
template <typename ColorT>
struct ColorHistogram {
  using MapType = std::map<ColorT, size_t>;

  ColorHistogram(const ColorT* pixels, size_t pixel_count) {
    for (size_t i = 0; i < pixel_count; ++i) {
      ++values[pixels[i]];
    }
  }
  ColorHistogram(const uint8_t* pixel_bytes, size_t pixel_count)
      : ColorHistogram(reinterpret_cast<const ColorT*>(pixel_bytes), pixel_count) {}

  ColorHistogram(const MapType& values_) : values(values_) {}

  // Return the number of occurrences of |color| in the histogram.
  size_t operator[](const ColorT& color) const {
    auto it = values.find(color);
    if (it != values.end()) {
      return it->second;
    }
    return 0;
  }

  // Return the number of distinct colors in the histogram.
  size_t size() const { return values.size(); }

  MapType values;
};

template <typename ColorT>
std::ostream& operator<<(std::ostream& os, const ColorHistogram<ColorT>& histogram) {
  if (histogram.values.size() <= 5) {
    // Print everything on one line.
    os << "ColorHistogram[";
    bool first = true;
    for (auto& pair : histogram.values) {
      if (first) {
        first = false;
      } else {
        os << ", ";
      }
      os << pair.first << "=" << pair.second;
    }
    os << "]";
  } else {
    os << "ColorHistogram(size=" << histogram.values.size() << ")[";
    for (auto& pair : histogram.values) {
      os << "\n  " << pair.first << "=" << pair.second;
    }
    os << "\n]";
  }
  return os;
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TYPES_COLOR_HISTOGRAM_H_
