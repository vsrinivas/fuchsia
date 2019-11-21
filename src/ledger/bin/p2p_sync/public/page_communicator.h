// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_PAGE_COMMUNICATOR_H_
#define SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_PAGE_COMMUNICATOR_H_

#include <memory>

#include "src/ledger/bin/p2p_sync/public/page_communicator.h"

namespace p2p_sync {
// PageCommunicator handles page-level data transfer between peers.
class PageCommunicator {
 public:
  PageCommunicator() = default;
  PageCommunicator(const PageCommunicator&) = delete;
  PageCommunicator& operator=(const PageCommunicator&) = delete;
  virtual ~PageCommunicator() = default;

  // Start should be called at most once. It signals that this page is active on
  // this device.
  virtual void Start() = 0;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_PAGE_COMMUNICATOR_H_
