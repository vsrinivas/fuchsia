// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_

#include <flatbuffers/flatbuffers.h>

#include "lib/fxl/functional/closure.h"
#include "peridot/bin/ledger/p2p_provider/public/types.h"
#include "peridot/bin/ledger/p2p_sync/impl/device_mesh.h"
#include "peridot/bin/ledger/p2p_sync/impl/message_generated.h"
#include "peridot/bin/ledger/p2p_sync/public/page_communicator.h"
#include "peridot/lib/convert/convert.h"

namespace p2p_sync {
class PageCommunicatorImplInspectorForTest;

class PageCommunicatorImpl : public PageCommunicator {
 public:
  PageCommunicatorImpl(std::string namespace_id,
                       std::string page_id,
                       DeviceMesh* mesh);
  ~PageCommunicatorImpl() override;

  void set_on_delete(fxl::Closure on_delete);

  // OnDeviceChange is called each time a device connects or unconnects.
  void OnDeviceChange(fxl::StringView remote_device,
                      p2p_provider::DeviceChangeType change_type);

  // Called when a new request arrived for this page from device |source|.
  void OnNewRequest(fxl::StringView source, const Request* message);

  // Called when a new response arrived for this page from device |source|.
  void OnNewResponse(fxl::StringView source, const Response* message);

  // PageCommunicator:
  void Start() override;

 private:
  friend class PageCommunicatorImplInspectorForTest;

  void CreateWatchStart(flatbuffers::FlatBufferBuilder* buffer);
  void CreateWatchStop(flatbuffers::FlatBufferBuilder* buffer);

  std::set<std::string, convert::StringViewComparator> interested_devices_;
  fxl::Closure on_delete_;
  bool started_ = false;
  bool in_destructor_ = false;

  const std::string namespace_id_;
  const std::string page_id_;
  DeviceMesh* const mesh_;
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_
