// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/ime/text_input_state_update_functions.h"

#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"

namespace ime {

void DeleteBackward(const mozart::TextInputStatePtr& current_state) {
  FXL_VLOG(1) << "Deleting character (state = " << *current_state << "')";

  int64_t& base = current_state->selection->base;
  int64_t& extent = current_state->selection->extent;
  std::string text = current_state->text.To<std::string>();
  current_state->revision++;

  if (base == -1 || extent == -1) {
    // There is no selection/cursor.  Move cursor to end of the text.
    FXL_DCHECK(base == -1 && extent == -1);
    base = extent = text.size();
  }

  if (base == extent) {
    if (base > 0) {
      // Change cursor to 1-char selection, so that it can be uniformly handled
      // by the selection-deletion code below.
      base--;
    } else {
      // Cursor is at beginning of text; there is nothing previous to delete.
      return;
    }
  }

  // Delete the current selection.
  FXL_DCHECK(base >= 0);
  FXL_DCHECK(base < extent);
  FXL_DCHECK(extent <= static_cast<int64_t>(text.size()));
  text.erase(base, extent - base);
  current_state->text = fidl::String(std::move(text));
  extent = base;
}

}  // namespace ime
