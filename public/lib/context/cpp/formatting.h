// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/fidl/cpp/bindings/formatting.h"

namespace maxwell {

std::ostream& operator<<(std::ostream& os, const FocusedState& state);
std::ostream& operator<<(std::ostream& os, const StoryMetadata& meta);
std::ostream& operator<<(std::ostream& os, const ModuleMetadata& meta);
std::ostream& operator<<(std::ostream& os, const EntityMetadata& meta);
std::ostream& operator<<(std::ostream& os, const ContextMetadata& meta);

std::ostream& operator<<(std::ostream& os, const ContextValue& value);
std::ostream& operator<<(std::ostream& os, const ContextSelector& selector);

std::ostream& operator<<(std::ostream& os, const ContextUpdate& update);
std::ostream& operator<<(std::ostream& os, const ContextQuery& query);

}  // namespace maxwell
