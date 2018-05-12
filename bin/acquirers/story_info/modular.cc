// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/acquirers/story_info/modular.h"

namespace maxwell {

std::string StoryStateToString(modular::StoryState state) {
  switch (state) {
    case modular::StoryState::RUNNING:
      return "RUNNING";
    case modular::StoryState::STOPPED:
      return "STOPPED";
    case modular::StoryState::ERROR:
      return "ERROR";
  }
}

// TODO(thatguy): This is currently duplicated from
// apps/modular/src/story_runner/story_controller_impl.cc.
// Don't duplicate this.
void XdrLinkPath(modular::XdrContext* const xdr,
                 modular::LinkPath* const data) {
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link_name", &data->link_name);
}

void XdrModuleData(modular::XdrContext* const xdr,
                   modular::ModuleData* const data) {
  xdr->Field("url", &data->module_url);
  xdr->Field("module_path", &data->module_path);
  xdr->Field("module_source", &data->module_source);
}

}  // namespace maxwell
