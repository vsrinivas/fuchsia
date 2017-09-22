// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/acquirers/story_info/modular.h"

namespace maxwell {

std::string StoryStateToString(modular::StoryState state) {
  // INITIAL
  // STARTING
  // RUNNING
  // DONE
  // STOPPED
  // ERROR
  switch (state) {
    case modular::StoryState::INITIAL:
      return "INITIAL";
    case modular::StoryState::STARTING:
      return "STARTING";
    case modular::StoryState::RUNNING:
      return "RUNNING";
    case modular::StoryState::DONE:
      return "DONE";
    case modular::StoryState::STOPPED:
      return "STOPPED";
    case modular::StoryState::ERROR:
      return "ERROR";
    default:
      FXL_LOG(FATAL) << "Unknown modular::StoryState value: " << state;
  }
}

// TODO(thatguy): This is currently duplicated from
// apps/modular/src/story_runner/story_storage_impl.cc.
void XdrLinkPath(modular::XdrContext* const xdr,
                 modular::LinkPath* const data) {
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link_name", &data->link_name);
}

void XdrModuleData(modular::XdrContext* const xdr,
                   modular::ModuleData* const data) {
  xdr->Field("url", &data->module_url);
  xdr->Field("module_path", &data->module_path);
  xdr->Field("default_link_path", &data->link_path, XdrLinkPath);
  xdr->Field("module_source", &data->module_source);
}

}  // namespace maxwell
