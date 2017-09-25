// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/bindings/formatting.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"

namespace maxwell {

std::ostream& operator<<(std::ostream& os, const SuggestionDisplay& o);
std::ostream& operator<<(std::ostream& os, const Suggestion& o);

}  // namespace maxwell
