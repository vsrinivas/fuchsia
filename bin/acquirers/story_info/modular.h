// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_MODULAR_H_
#define APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_MODULAR_H_

#include <string>

#include "lib/module/fidl/module_data.fidl.h"
#include "lib/story/fidl/story_state.fidl.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace maxwell {

std::string StoryStateToString(modular::StoryState state);

void XdrLinkPath(modular::XdrContext* xdr, modular::LinkPath* data);

void XdrModuleData(modular::XdrContext* xdr, modular::ModuleData* data);

}  // namespace maxwell

#endif  // APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_MODULAR_H_
