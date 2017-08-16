// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <openssl/sha.h>
#include <regex>
#include <sstream>

#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "lib/ftl/logging.h"

namespace maxwell {

ContextSelectorPtr ComponentScopeToContextSelector(const ComponentScopePtr& scope) {
  ContextSelectorPtr selector;
  if (!scope || scope->is_global_scope()) return selector;
  selector = ContextSelector::New();
  if (scope->is_module_scope()) {
    selector->type = ContextValueType::MODULE;
    selector->meta = ContextMetadata::New();
    selector->meta->story = StoryMetadata::New();
    selector->meta->story->id = scope->get_module_scope()->story_id;
    selector->meta->mod = ModuleMetadata::New();
    selector->meta->mod->path =
        scope->get_module_scope()->module_path.Clone();
  } else if (scope->is_agent_scope()) {
    // TODO(thatguy)
  } else if (scope->is_story_scope()) {
    selector->type = ContextValueType::STORY;
    selector->meta = ContextMetadata::New();
    selector->meta->story = StoryMetadata::New();
    selector->meta->story->id = scope->get_story_scope()->story_id;
  }

  return selector;
}

}  // namespace maxwell
