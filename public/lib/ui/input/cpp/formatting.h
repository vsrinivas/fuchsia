// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SERVICES_INPUT_CPP_FORMATTING_H_
#define APPS_MOZART_SERVICES_INPUT_CPP_FORMATTING_H_

#include <iosfwd>

#include "lib/ui/input/fidl/input_event_constants.fidl.h"
#include "lib/ui/input/fidl/input_events.fidl.h"
#include "lib/ui/input/fidl/input_reports.fidl.h"
#include "lib/ui/input/fidl/text_editing.fidl.h"
#include "lib/ui/input/fidl/text_input.fidl.h"

namespace mozart {

std::ostream& operator<<(std::ostream& os, const InputEvent& value);
std::ostream& operator<<(std::ostream& os, const PointerEvent& value);
std::ostream& operator<<(std::ostream& os, const KeyboardEvent& value);

std::ostream& operator<<(std::ostream& os, const Range& value);
std::ostream& operator<<(std::ostream& os, const Axis& value);

std::ostream& operator<<(std::ostream& os, const KeyboardDescriptor& value);
std::ostream& operator<<(std::ostream& os, const MouseDescriptor& value);
std::ostream& operator<<(std::ostream& os, const StylusDescriptor& value);
std::ostream& operator<<(std::ostream& os, const TouchscreenDescriptor& value);
std::ostream& operator<<(std::ostream& os, const DeviceDescriptor& value);

std::ostream& operator<<(std::ostream& os, const KeyboardReport& value);
std::ostream& operator<<(std::ostream& os, const MouseReport& value);
std::ostream& operator<<(std::ostream& os, const StylusReport& value);
std::ostream& operator<<(std::ostream& os, const Touch& value);
std::ostream& operator<<(std::ostream& os, const TouchscreenReport& value);
std::ostream& operator<<(std::ostream& os, const InputReport& value);


std::ostream& operator<<(std::ostream& os, const TextSelection& value);
std::ostream& operator<<(std::ostream& os, const TextRange& value);
std::ostream& operator<<(std::ostream& os, const TextInputState& value);

}  // namespace mozart

#endif  // APPS_MOZART_SERVICES_INPUT_CPP_FORMATTING_H_
