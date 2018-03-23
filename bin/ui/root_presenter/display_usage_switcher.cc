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

static const std::array<presentation::DisplayUsage, 5> kDisplayUsages = {
    presentation::DisplayUsage::HANDHELD, presentation::DisplayUsage::CLOSE,
    presentation::DisplayUsage::NEAR, presentation::DisplayUsage::MIDRANGE,
    presentation::DisplayUsage::FAR};
}  // namespace

std::string GetDisplayUsageAsString(presentation::DisplayUsage usage) {
  switch (usage) {
    case presentation::DisplayUsage::UNKNOWN:
      return "UNKNOWN";
    case presentation::DisplayUsage::HANDHELD:
      return "HANDHELD";
    case presentation::DisplayUsage::CLOSE:
      return "CLOSE";
    case presentation::DisplayUsage::NEAR:
      return "NEAR";
    case presentation::DisplayUsage::MIDRANGE:
      return "MIDRANGE";
    case presentation::DisplayUsage::FAR:
      return "FAR";
  }
}

DisplayUsageSwitcher::DisplayUsageSwitcher() {}

bool DisplayUsageSwitcher::OnEvent(const input::InputEvent& event,
                                   Presentation* presenter) {
  if (event.is_keyboard()) {
    const input::KeyboardEvent& kbd = event.keyboard();
    if ((kbd.modifiers & input::kModifierAlt) &&
        kbd.phase == input::KeyboardEventPhase::PRESSED &&
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
