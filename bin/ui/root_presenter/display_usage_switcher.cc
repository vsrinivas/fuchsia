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

static const std::array<mozart::DisplayUsage, 5> kDisplayUsages = {
    mozart::DisplayUsage::HANDHELD, mozart::DisplayUsage::CLOSE,
    mozart::DisplayUsage::NEAR, mozart::DisplayUsage::MIDRANGE,
    mozart::DisplayUsage::FAR};
}  // namespace

std::string GetDisplayUsageAsString(mozart::DisplayUsage usage) {
  switch (usage) {
    case mozart::DisplayUsage::UNKNOWN:
      return "UNKNOWN";
    case mozart::DisplayUsage::HANDHELD:
      return "HANDHELD";
    case mozart::DisplayUsage::CLOSE:
      return "CLOSE";
    case mozart::DisplayUsage::NEAR:
      return "NEAR";
    case mozart::DisplayUsage::MIDRANGE:
      return "MIDRANGE";
    case mozart::DisplayUsage::FAR:
      return "FAR";
  }
}

DisplayUsageSwitcher::DisplayUsageSwitcher() {}

bool DisplayUsageSwitcher::OnEvent(const mozart::InputEventPtr& event,
                                   Presentation* presenter) {
  if (event->is_keyboard()) {
    const mozart::KeyboardEventPtr& kbd = event->get_keyboard();
    if ((kbd->modifiers & mozart::kModifierAlt) &&
        kbd->phase == mozart::KeyboardEvent::Phase::PRESSED &&
        kbd->code_point == kGlobalShortcutKeyCodePoint &&
        kbd->hid_usage == kGlobalShortcutKeyHidUsage) {
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
