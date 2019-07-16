// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "src/ui/a11y/lib/screen_reader/actions.h"

namespace a11y {

// The Fuchsia Screen Reader.
//
// This is the base class for the Fuchsia Screen Reader. It connects to all
// services necessary to make a funcional Screen Reader.
//
// A common loop would be something like:
//   User performes some sort of input (via touch screen for example). The input
//   triggers an Screen Reader action, which then calls the Fuchsia
//   Accessibility APIs. Finally, some output is communicated (via speech, for
//   example).
// TODO(MI4-2546): Rename this class once the final screen reader name exists.
class ScreenReader {
 public:
  ScreenReader() = default;
  ~ScreenReader() = default;

  // Initializes the Screen Reader, connecting to services and biding inputs to
  // Screen Reader Actions.
  void Init() {}

 private:
  // Maps action names to screen reader actions.
  std::unordered_map<std::string, std::unique_ptr<ScreenReaderAction>> actions_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_H_
