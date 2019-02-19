// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_MODEL_TESTING_MUTATION_MATCHERS_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_MODEL_TESTING_MUTATION_MATCHERS_H_

#include <fuchsia/modular/storymodel/cpp/fidl.h>

#include "gmock/gmock.h"  // For MATCHER macros.
#include "lib/fostr/fidl/fuchsia/modular/formatting.h"
#include "lib/fostr/fidl/fuchsia/modular/storymodel/formatting.h"

namespace modular {

// |arg| is a StoryModelMutation, |expected| is a StoryVisibilitystate.
MATCHER_P(IsSetVisibilityMutation, expected, "") {
  *result_listener << "is set_visibility_state " << expected;
  if (!arg.is_set_visibility_state())
    return false;
  return expected == arg.set_visibility_state();
}

// |arg| is a StoryModelMutation, |expected| is a StoryState.
MATCHER_P(IsSetRuntimeStateMutation, expected, "") {
  *result_listener << "is set_runtime_state " << expected;
  if (!arg.is_set_runtime_state())
    return false;
  return expected == arg.set_runtime_state();
}

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_MODEL_TESTING_MUTATION_MATCHERS_H_
