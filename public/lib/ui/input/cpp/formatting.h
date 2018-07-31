// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_INPUT_CPP_FORMATTING_H_
#define LIB_UI_INPUT_CPP_FORMATTING_H_

#include <iosfwd>

#include <fuchsia/ui/input/cpp/fidl.h>

namespace fuchsia {
namespace ui {
namespace input {

// NOTE:
// //garnet/public/lib/fostr/fidl/fuchsia.ui.input generates ostream formatters
// for this library *except* those formatters that are listed here. The code
// generator knows which formatters to exclude from the generated code by
// consulting the 'amendments.json' file in that directory.
//
// If you add or remove formatters from this file, please be sure that the
// amendments.json file is updated accordingly.

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::InputEvent& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::PointerEvent& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::KeyboardEvent& value);

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::Range& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::Axis& value);

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::KeyboardDescriptor& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::MouseDescriptor& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::StylusDescriptor& value);
std::ostream& operator<<(
    std::ostream& os, const fuchsia::ui::input::TouchscreenDescriptor& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::SensorDescriptor& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::DeviceDescriptor& value);

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::KeyboardReport& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::MouseReport& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::StylusReport& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::Touch& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::TouchscreenReport& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::SensorReport& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::InputReport& value);

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::TextSelection& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::TextRange& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::TextInputState& value);

}  // namespace input
}  // namespace ui
}  // namespace fuchsia

#endif  // LIB_UI_INPUT_CPP_FORMATTING_H_
