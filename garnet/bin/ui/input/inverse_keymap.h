// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_INVERSE_KEYMAP_H_
#define GARNET_BIN_UI_INPUT_INVERSE_KEYMAP_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid/hid.h>
#include <hid/usages.h>

#include <map>
#include <utility>
#include <vector>

namespace input {

struct Keystroke {
  uint32_t usage;
  enum class Shift {
    kNo,
    kYes,
    kDontCare,
  } shift;
};

// Lightweight utility for basic keymap conversion of chars to keystrokes. This
// is intended for end-to-end and input testing only; for production use cases
// and general testing, IME injection should be used instead. Generally a
// mapping exists only for printable ASCII characters; in particular neither \t
// nor \n is mapped in either of the standard zircon keymaps. Furthermore, IME
// implementations may themselves override the keymap in a way that invalidates
// this translation.
//
// This is an inverse of hid/hid.h:hid_map_key.
using InverseKeymap = std::map<char, Keystroke>;
using KeySequence = std::vector<fuchsia::ui::input::KeyboardReportPtr>;

// Constructs an inverse keymap from a keymap with |KEYMAP_SIZE| entries.
InverseKeymap InvertKeymap(const keychar_t keymap[]);

// Builds a key sequence representing the given string under the provided
// |InverseKeymap|.
//
// This is intended for end-to-end and input testing only; for production use
// cases and general testing, IME injection should be used instead.
//
// A translation from |text| to a sequence of keystrokes is not guaranteed to
// exist. If a translation does not exist, false is returned for the bool
// member of the pair. See |InverseKeymap| for details.
//
// The sequence does not contain pauses except between repeated keys or to clear
// a shift state, though the sequence does terminate with an empty report (no
// keys pressed). A shift key transition is sent in advance of each series of
// keys that needs it.
//
// Returns { key sequence, success }.
std::pair<KeySequence, bool> DeriveKeySequence(const InverseKeymap& inverse_keymap,
                                               const std::string& text);

}  // namespace input
#endif  // GARNET_BIN_UI_INPUT_INVERSE_KEYMAP_H_
