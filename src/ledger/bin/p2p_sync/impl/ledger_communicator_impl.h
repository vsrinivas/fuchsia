// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_LEDGER_COMMUNICATOR_IMPL_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_LEDGER_COMMUNICATOR_IMPL_H_

#include <lib/fit/function.h>

#import <map>

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/p2p_provider/public/types.h"
#include "src/ledger/bin/p2p_sync/impl/device_mesh.h"
#include "src/ledger/bin/p2p_sync/impl/message_generated.h"
#include "src/ledger/bin/p2p_sync/impl/message_holder.h"
#include "src/ledger/bin/p2p_sync/public/ledger_communicator.h"
#include "src/ledger/bin/p2p_sync/public/page_communicator.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace p2p_sync {
class PageCommunicatorImpl;

// Ledger-level P2P communicator.
class LedgerCommunicatorImpl : public LedgerCommunicator {
 public:
  LedgerCommunicatorImpl(ledger::Environment* environment, std::string namespace_id,
                         DeviceMesh* mesh);
  ~LedgerCommunicatorImpl() override;

  void set_on_delete(fit::closure on_delete);

  // OnDeviceChange is called each time a device connects or unconnects.
  void OnDeviceChange(const p2p_provider::P2PClientId& remote_device,
                      p2p_provider::DeviceChangeType change_type);

  // Called when a new request arrived for this ledger from device |source|.
  void OnNewRequest(const p2p_provider::P2PClientId& source, fxl::StringView page_id,
                    MessageHolder<Request> message);

  // Called when a new response arrived for this ledger from device |source|.
  void OnNewResponse(const p2p_provider::P2PClientId& source, fxl::StringView page_id,
                     MessageHolder<Response> message);

  // LedgerCommunicator:
  std::unique_ptr<PageCommunicator> GetPageCommunicator(
      storage::PageStorage* storage, storage::PageSyncClient* sync_client) override;

 private:
  std::map<std::string, PageCommunicatorImpl*, convert::StringViewComparator> pages_;

  fit::closure on_delete_;
  ledger::Environment* environment_;
  const std::string namespace_id_;
  DeviceMesh* const mesh_;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_LEDGER_COMMUNICATOR_IMPL_H_
