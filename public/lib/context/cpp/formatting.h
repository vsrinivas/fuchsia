// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "lib/fidl/cpp/bindings/formatting.h"

namespace maxwell {

std::ostream& operator<<(std::ostream& os, const ContextUpdateForTopics& update);

std::ostream& operator<<(std::ostream& os, const ContextQueryForTopics& query);

}  // namespace maxwell
