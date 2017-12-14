// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/display_usage_switcher.h"

#include <array>

#include "garnet/bin/ui/root_presenter/presentation.h"

namespace root_presenter {
namespace {

static const std::array<mozart::DisplayUsage, 5> kDisplayUsages = {
    mozart::DisplayUsage::HANDHELD, mozart::DisplayUsage::CLOSE,
    mozart::DisplayUsage::NEAR, mozart::DisplayUsage::MIDRANGE,
    mozart::DisplayUsage::FAR};
}  // namespace

DisplayUsageSwitcher::DisplayUsageSwitcher() {}

bool DisplayUsageSwitcher::OnEvent(const mozart::InputEventPtr& event,
                                   Presentation* presenter,
                                   bool* continue_dispatch_out) {
  FXL_DCHECK(continue_dispatch_out);
  bool invalidate = false;
  if (event->is_keyboard()) {
    const mozart::KeyboardEventPtr& kbd = event->get_keyboard();
    const uint32_t kEqualsKeyCodePoint = 61;
    const uint32_t kEqualsKeyHidUsage = 46;
    if ((kbd->modifiers & mozart::kModifierAlt) &&
        kbd->phase == mozart::KeyboardEvent::Phase::PRESSED &&
        kbd->code_point == kEqualsKeyCodePoint &&
        kbd->hid_usage == kEqualsKeyHidUsage) {
      // Switch to the next display usage value.
      current_display_usage_index_ =
          (current_display_usage_index_ + 1) % kDisplayUsages.size();
      presenter->SetDisplayUsage(kDisplayUsages[current_display_usage_index_]);

      invalidate = true;
      *continue_dispatch_out = false;
    }
  }

  return invalidate;
}

}  // namespace root_presenter
