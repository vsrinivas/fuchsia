// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_LEDGER_COMMUNICATOR_IMPL_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_LEDGER_COMMUNICATOR_IMPL_H_

#import <map>

#include <lib/fit/function.h>

#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/p2p_provider/public/types.h"
#include "peridot/bin/ledger/p2p_sync/impl/device_mesh.h"
#include "peridot/bin/ledger/p2p_sync/impl/message_generated.h"
#include "peridot/bin/ledger/p2p_sync/impl/message_holder.h"
#include "peridot/bin/ledger/p2p_sync/public/ledger_communicator.h"
#include "peridot/bin/ledger/p2p_sync/public/page_communicator.h"
#include "peridot/lib/convert/convert.h"

namespace p2p_sync {
class PageCommunicatorImpl;

// Ledger-level P2P communicator.
class LedgerCommunicatorImpl : public LedgerCommunicator {
 public:
  LedgerCommunicatorImpl(coroutine::CoroutineService* coroutine_service,
                         std::string namespace_id, DeviceMesh* mesh);
  ~LedgerCommunicatorImpl() override;

  void set_on_delete(fit::closure on_delete);

  // OnDeviceChange is called each time a device connects or unconnects.
  void OnDeviceChange(fxl::StringView remote_device,
                      p2p_provider::DeviceChangeType change_type);

  // Called when a new request arrived for this ledger from device |source|.
  void OnNewRequest(fxl::StringView source, fxl::StringView page_id,
                    MessageHolder<Request> message);

  // Called when a new response arrived for this ledger from device |source|.
  void OnNewResponse(fxl::StringView source, fxl::StringView page_id,
                     MessageHolder<Response> message);

  // LedgerCommunicator:
  std::unique_ptr<PageCommunicator> GetPageCommunicator(
      storage::PageStorage* storage,
      storage::PageSyncClient* sync_client) override;

 private:
  std::map<std::string, PageCommunicatorImpl*, convert::StringViewComparator>
      pages_;

  fit::closure on_delete_;
  coroutine::CoroutineService* const coroutine_service_;
  const std::string namespace_id_;
  DeviceMesh* const mesh_;
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_LEDGER_COMMUNICATOR_IMPL_H_
