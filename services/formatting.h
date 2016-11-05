// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/context_engine.fidl.h"
#include "apps/maxwell/services/suggestion_manager.fidl.h"
#include "lib/fidl/cpp/bindings/formatting.h"

namespace maxwell {
namespace context_engine {

std::ostream& operator<<(std::ostream& os, const ContextUpdate& o);

}  // namespace context_engine

namespace suggestion_engine {

std::ostream& operator<<(std::ostream& os,
                         const SuggestionDisplayProperties& o);

std::ostream& operator<<(std::ostream& os, const Suggestion& o);

}  // namespace suggestion_engine
}  // namespace maxwell
