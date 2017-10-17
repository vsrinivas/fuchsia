// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/suggestion/fidl/user_input.fidl.h"

namespace maxwell {

enum QueryType { NONE = 0, TEXT, SPEECH, SELECTION, OTHER };

// TODO(jwnichols): This has some overlap with the UserInput object passed
// through FIDL during queries. Think about how to unify the two.
struct QueryContext {
  QueryType type;
  std::string query;
};

}  // namespace maxwell
