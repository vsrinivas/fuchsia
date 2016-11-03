// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_READER_INPUT_DESCRIPTOR_H_
#define APPS_MOZART_SRC_INPUT_READER_INPUT_DESCRIPTOR_H_

#include <stdint.h>

#include <map>
#include <utility>
#include <vector>

namespace mozart {
namespace input {

#define INPUT_USAGE_BUTTON_PRIMARY 0x01
#define INPUT_USAGE_BUTTON_SECONDARY 0x02
#define INPUT_USAGE_BUTTON_TERTIARY 0x04

#define INPUT_USAGE_STYLUS_TIP 0x02
#define INPUT_USAGE_STYLUS_BARREL 0x04
#define INPUT_USAGE_STYLUS_INVERT 0x08
#define INPUT_USAGE_STYLUS_ERASER 0x10

template <class T>
struct Range {
  T min;
  T max;
};

template <class T>
struct Axis {
  Range<T> range;
  T resolution;
};

template <class T>
Axis<T> MakeAxis(T min, T max, T resolution) {
  return {
      .range = {.min = min, .max = max}, .resolution = resolution,
  };
}

using KeyUsage = uint32_t;
using ButtonUsage = uint32_t;
using SwitchUsage = uint32_t;
using AxisUsage = uint32_t;

struct InputDescriptor {};

struct MouseDescriptor : public InputDescriptor {
  void AddButton(ButtonUsage button) { buttons.push_back(button); }

  std::vector<ButtonUsage> buttons;
  Axis<int32_t> rel_x;
  Axis<int32_t> rel_y;
  Axis<int32_t> vscroll;
  Axis<int32_t> hscroll;
};

struct KeyboardDescriptor : public InputDescriptor {
  void AddKey(KeyUsage key) { keys.push_back(key); }
  void AddKeyRange(KeyUsage from, KeyUsage to) {
    for (KeyUsage key = from; key < to; key++) {
      AddKey(key);
    }
  }

  std::vector<KeyUsage> keys;
};

struct StylusDescriptor : public InputDescriptor {
  void AddButton(ButtonUsage button) { buttons.push_back(button); }

  std::vector<KeyUsage> buttons;
  Axis<uint32_t> x;
  Axis<uint32_t> y;
};

struct TouchscreenDescriptor : public InputDescriptor {
  Axis<uint32_t> x;
  Axis<uint32_t> y;
};

}  // namespace input
}  // namespace mozart

#endif  // APPS_MOZART_SRC_INPUT_READER_INPUT_DESCRIPTOR_H_
