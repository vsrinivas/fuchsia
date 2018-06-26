  // Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/scope_utils.h"

#include <regex>
#include <sstream>

#include <openssl/sha.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/vector.h"
#include "lib/fxl/logging.h"

namespace maxwell {

fuchsia::modular::ContextSelectorPtr ComponentScopeToContextSelector(
    const fuchsia::modular::ComponentScopePtr& scope) {
  fuchsia::modular::ContextSelectorPtr selector;
  if (!scope || scope->is_global_scope())
    return selector;
  selector = fuchsia::modular::ContextSelector::New();
  if (scope->is_module_scope()) {
    selector->type = fuchsia::modular::ContextValueType::MODULE;
    selector->meta = fuchsia::modular::ContextMetadata::New();
    selector->meta->story = fuchsia::modular::StoryMetadata::New();
    selector->meta->story->id = scope->module_scope().story_id;
    selector->meta->mod = fuchsia::modular::ModuleMetadata::New();
    fidl::VectorPtr<fidl::StringPtr> path;
    fidl::Clone(scope->module_scope().module_path, &selector->meta->mod->path);
  } else if (scope->is_agent_scope()) {
    // TODO(thatguy): do.
  } else if (scope->is_story_scope()) {
    selector->type = fuchsia::modular::ContextValueType::STORY;
    selector->meta = fuchsia::modular::ContextMetadata::New();
    selector->meta->story = fuchsia::modular::StoryMetadata::New();
    selector->meta->story->id = scope->story_scope().story_id;
  }

  return selector;
}

}  // namespace maxwell
