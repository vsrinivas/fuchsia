// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_KEY_UTIL_H_
#define APPS_MOTERM_KEY_UTIL_H_

#include <string>

namespace mozart {
class Event;
}

// Gets an appropriate (VT100/xterm-ish) sequence of characters to generate for
// the given key pressed event (|key_event| must have |action| value
// |EventType::KEY_PRESSED|). |keypad_application_mode| indicates if the keypad
// is in "application mode".
std::string GetInputSequenceForKeyPressedEvent(const mozart::Event& key_event,
                                               bool keypad_application_mode);

#endif  // APPS_MOTERM_KEY_UTIL_H_
