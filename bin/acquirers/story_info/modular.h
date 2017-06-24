// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_MODULAR_H_
#define APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_MODULAR_H_

#include <string>

#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/story_state.fidl.h"

namespace maxwell {

std::string StoryStateToString(modular::StoryState state);

void XdrLinkPath(modular::XdrContext* xdr,
                 modular::LinkPath* data);

void XdrModuleData(modular::XdrContext* xdr,
                   modular::ModuleData* data);

}  // namespace maxwell

#endif  // APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_MODULAR_H_
