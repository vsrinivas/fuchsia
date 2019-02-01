// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/display_size_switcher.h"

#include <array>

#include "garnet/bin/ui/root_presenter/presentation.h"

namespace root_presenter {
namespace {

using DisplaySize = std::pair<float, float>;

// Baked set of display sizes using for testing.
static const std::array<DisplaySize, 5> kDisplaySizesInMm = {
    DisplaySize(0, 0),            // native size
    DisplaySize(110.69, 62.26),   // 5in 16:9
    DisplaySize(154.97, 87.17),   // 7in 16:9
    DisplaySize(221.38, 124.53),  // 10in 16:9
    DisplaySize(265.66, 149.43)   // 12in 16:9
};

// Global keyboard shortcut for switching display size.
const uint32_t kGlobalShortcutKeyCodePoint = 45;  // '-' key
const uint32_t kGlobalShortcutKeyHidUsage = 45;   // '-' key

}  // namespace

bool DisplaySizeSwitcher::OnEvent(const fuchsia::ui::input::InputEvent& event,
                                  Presentation* presenter) {
  if (event.is_keyboard()) {
    const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();
    if ((kbd.modifiers & fuchsia::ui::input::kModifierAlt) &&
        kbd.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED &&
        kbd.code_point == kGlobalShortcutKeyCodePoint &&
        kbd.hid_usage == kGlobalShortcutKeyHidUsage) {
      // Switch to the next display size we can successfully switch to.
      for (size_t offset = 1; offset < kDisplaySizesInMm.size(); ++offset) {
        size_t display_size_index =
            (current_display_size_index_ + offset) % kDisplaySizesInMm.size();
        DisplaySize display_size = kDisplaySizesInMm[display_size_index];

        // Check if display size can be applied (e.g. can return false the
        // requested size is bigger than the actual display size).
        if (presenter->SetDisplaySizeInMmWithoutApplyingChanges(
                display_size.first, display_size.second, false)) {
          // Found a suitable display size to switch to.
          current_display_size_index_ = display_size_index;

          presenter->ApplyDisplayModelChanges(true, false);
          return true;
        }
      }
    }
  }
  return false;
};

}  // namespace root_presenter
