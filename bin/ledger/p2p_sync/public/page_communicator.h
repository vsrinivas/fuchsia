// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_PAGE_COMMUNICATOR_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_PAGE_COMMUNICATOR_H_

#include "peridot/bin/ledger/p2p_sync/public/page_communicator.h"

#include <memory>

#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace p2p_sync {
// PageCommunicator handles page-level data transfer between peers.
class PageCommunicator {
 public:
  PageCommunicator() {}
  virtual ~PageCommunicator() {}

  // Start should be called at most once. It signals that this page is active on
  // this device.
  virtual void Start() = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageCommunicator);
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_PAGE_COMMUNICATOR_H_
