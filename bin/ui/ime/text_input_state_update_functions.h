// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_TEXT_INPUT_STATE_UPDATE_FUNCTIONS_H_
#define GARNET_BIN_UI_TEXT_INPUT_STATE_UPDATE_FUNCTIONS_H_

#include <memory>
#include <vector>

#include "lib/ui/input/fidl/text_input.fidl.h"

namespace ime {

// Update the provided state as follows:
// - if there is a selected region, delete it.
// - else, if the cursor is not at the beginning of the text, delete the
//   character immediately preceding the cursor.
// - else, no update occurs.
void DeleteBackward(const mozart::TextInputStatePtr& current_state);

}  // namespace ime

#endif  // GARNET_BIN_UI_TEXT_INPUT_STATE_UPDATE_FUNCTIONS_H_
