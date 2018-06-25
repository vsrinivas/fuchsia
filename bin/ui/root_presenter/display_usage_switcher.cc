// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/display_usage_switcher.h"

#include <array>

#include "garnet/bin/ui/root_presenter/presentation.h"

namespace root_presenter {
namespace {

// Global keyboard shortcut for switching display usage.
const uint32_t kGlobalShortcutKeyCodePoint = 61;  // '=' key
const uint32_t kGlobalShortcutKeyHidUsage = 46;   // '=' key

static const std::array<fuchsia::ui::policy::DisplayUsage, 5> kDisplayUsages = {
    fuchsia::ui::policy::DisplayUsage::kHandheld,
    fuchsia::ui::policy::DisplayUsage::kClose,
    fuchsia::ui::policy::DisplayUsage::kNear,
    fuchsia::ui::policy::DisplayUsage::kMidrange,
    fuchsia::ui::policy::DisplayUsage::kFar};
}  // namespace

std::string GetDisplayUsageAsString(fuchsia::ui::policy::DisplayUsage usage) {
  switch (usage) {
    case fuchsia::ui::policy::DisplayUsage::kUnknown:
      return "kUnknown";
    case fuchsia::ui::policy::DisplayUsage::kHandheld:
      return "kHandheld";
    case fuchsia::ui::policy::DisplayUsage::kClose:
      return "kClose";
    case fuchsia::ui::policy::DisplayUsage::kNear:
      return "kNear";
    case fuchsia::ui::policy::DisplayUsage::kMidrange:
      return "kMidrange";
    case fuchsia::ui::policy::DisplayUsage::kFar:
      return "kFar";
  }
}

DisplayUsageSwitcher::DisplayUsageSwitcher() {}

bool DisplayUsageSwitcher::OnEvent(const fuchsia::ui::input::InputEvent& event,
                                   Presentation* presenter) {
  if (event.is_keyboard()) {
    const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();
    if ((kbd.modifiers & fuchsia::ui::input::kModifierAlt) &&
        kbd.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED &&
        kbd.code_point == kGlobalShortcutKeyCodePoint &&
        kbd.hid_usage == kGlobalShortcutKeyHidUsage) {
      // Switch to the next display usage value.
      current_display_usage_index_ =
          (current_display_usage_index_ + 1) % kDisplayUsages.size();
      presenter->SetDisplayUsage(kDisplayUsages[current_display_usage_index_]);

      return true;
    }
  }

  return false;
}

}  // namespace root_presenter
