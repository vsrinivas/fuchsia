// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/src/agents/entity_utils/entity_span.h"

namespace maxwell {

// The EntitySpan type field for email entities.
const std::string kEmailType = "email";

// Context Engine topic for raw text output from basic_text_reporter.
const std::string kRawTextTopic = "/story/focused/explicit/raw/text";

// Context Engine topic for text selection output from basic_text_reporter.
const std::string kRawTextSelectionTopic =
    "/story/focused/explicit/raw/text_selection";

// Context Engine topic for entities found by BasicTextListener.
const std::string kFocalEntitiesTopic = "/inferred/focal_entities";

// Context Engine topic for selected entities from SelectedEntityFinder.
const std::string kSelectedEntitiesTopic = "/inferred/selected_entities";

// Return true iff key is in the Context Engine update given by result,
// and the value of key is not null.
bool KeyInUpdateResult(const ContextUpdatePtr& result, const std::string& key);

}  // namespace maxwell
