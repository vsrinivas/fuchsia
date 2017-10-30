// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_AGENTS_ENTITY_UTILS_ENTITY_UTILS_H_
#define PERIDOT_BIN_AGENTS_ENTITY_UTILS_ENTITY_UTILS_H_

#include <string>

namespace maxwell {

// The EntitySpan type field for email entities.
const std::string kEmailType = "email";

// Context Engine topic for raw text output from basic_text_reporter.
const std::string kRawTextTopic = "raw/text";

// Context Engine topic for text selection output from basic_text_reporter.
const std::string kRawTextSelectionTopic = "raw/text_selection";

// Context Engine topic for entities found by BasicTextListener.
const std::string kFocalEntitiesTopic = "inferred/focal_entities";

// Context Engine topic for selected entities from SelectedEntityFinder.
const std::string kSelectedEntitiesTopic = "inferred/selected_entities";

}  // namespace maxwell

#endif  // PERIDOT_BIN_AGENTS_ENTITY_UTILS_ENTITY_UTILS_H_
