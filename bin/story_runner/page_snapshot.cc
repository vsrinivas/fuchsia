// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/page_snapshot.h"

namespace modular {

PageSnapshot::PageSnapshot(const std::string& context) : context_(context) {}

PageSnapshot::~PageSnapshot() = default;

fidl::InterfaceRequest<ledger::PageSnapshot> PageSnapshot::NewRequest() {
  page_snapshot_.reset(new ledger::PageSnapshotPtr);
  auto ret = (*page_snapshot_).NewRequest();
  (*page_snapshot_).set_connection_error_handler([this] {
    FTL_LOG(ERROR) << context_ << ": "
                   << "PageSnapshot connection unexpectedly closed.";
  });
  return ret;
}

}  // namespace modular
