// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_INPUT_CPP_FORMATTING_H_
#define LIB_UI_INPUT_CPP_FORMATTING_H_

#include <iosfwd>

#include <fuchsia/cpp/input.h>

namespace input {

std::ostream& operator<<(std::ostream& os, const input::InputEvent& value);
std::ostream& operator<<(std::ostream& os, const input::PointerEvent& value);
std::ostream& operator<<(std::ostream& os, const input::KeyboardEvent& value);

std::ostream& operator<<(std::ostream& os, const input::Range& value);
std::ostream& operator<<(std::ostream& os, const input::Axis& value);

std::ostream& operator<<(std::ostream& os,
                         const input::KeyboardDescriptor& value);
std::ostream& operator<<(std::ostream& os, const input::MouseDescriptor& value);
std::ostream& operator<<(std::ostream& os,
                         const input::StylusDescriptor& value);
std::ostream& operator<<(std::ostream& os,
                         const input::TouchscreenDescriptor& value);
std::ostream& operator<<(std::ostream& os,
                         const input::DeviceDescriptor& value);

std::ostream& operator<<(std::ostream& os, const input::KeyboardReport& value);
std::ostream& operator<<(std::ostream& os, const input::MouseReport& value);
std::ostream& operator<<(std::ostream& os, const input::StylusReport& value);
std::ostream& operator<<(std::ostream& os, const input::Touch& value);
std::ostream& operator<<(std::ostream& os,
                         const input::TouchscreenReport& value);
std::ostream& operator<<(std::ostream& os, const input::InputReport& value);

std::ostream& operator<<(std::ostream& os, const input::TextSelection& value);
std::ostream& operator<<(std::ostream& os, const input::TextRange& value);
std::ostream& operator<<(std::ostream& os, const input::TextInputState& value);

}  // namespace input

#endif  // LIB_UI_INPUT_CPP_FORMATTING_H_
