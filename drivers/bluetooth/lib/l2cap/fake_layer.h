// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_FAKE_LAYER_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_FAKE_LAYER_H_

#include <unordered_map>
#include <utility>

#include "garnet/drivers/bluetooth/lib/hci/connection_parameters.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"

namespace btlib {
namespace l2cap {
namespace testing {

// This is a fake version the root L2CAP object that can be injected into the
// GAP layer for unit testing.
class FakeLayer final : public L2CAP {
 public:
  inline static fbl::RefPtr<FakeLayer> Create() {
    return fbl::AdoptRef(new FakeLayer());
  }

  // Triggers a LE connection parameter update callback on the given link.
  void TriggerLEConnectionParameterUpdate(
      hci::ConnectionHandle handle,
      const hci::LEPreferredConnectionParameters& params);

  // Triggers the creation of an inbound dynamic channel on the given link. The
  // channels created will be provided to handlers passed to RegisterService.
  void TriggerInboundChannel(hci::ConnectionHandle handle, PSM psm,
                             ChannelId id, ChannelId remote_id);

  // L2CAP overrides
  void Initialize() override;
  void ShutDown() override;
  void AddACLConnection(hci::ConnectionHandle handle,
                        hci::Connection::Role role,
                        LinkErrorCallback link_error_callback,
                        async_dispatcher_t* dispatcher) override;
  void AddLEConnection(hci::ConnectionHandle handle, hci::Connection::Role role,
                       LEConnectionParameterUpdateCallback conn_param_callback,
                       LinkErrorCallback link_error_callback,
                       AddLEConnectionCallback channel_callback,
                       async_dispatcher_t* dispatcher) override;
  void RemoveConnection(hci::ConnectionHandle handle) override;
  bool RegisterService(PSM psm, ChannelCallback cb,
                       async_dispatcher_t* dispatcher) override;
  void UnregisterService(PSM psm) override;

  // Called when a new channel gets opened. Tests can use this to obtain a
  // reference to all channels.
  void set_channel_callback(ChannelCallback callback) {
    chan_cb_ = std::move(callback);
  }

 private:
  friend class fbl::RefPtr<FakeLayer>;

  using ChannelDelivery = std::pair<ChannelCallback, async_dispatcher_t*>;

  struct LinkData {
    hci::ConnectionHandle handle;
    hci::Connection::Role role;
    hci::Connection::LinkType type;

    async_dispatcher_t* dispatcher;

    // Dual-mode callbacks
    LinkErrorCallback link_error_cb;

    // LE-only callbacks
    LEConnectionParameterUpdateCallback le_conn_param_cb;
  };

  FakeLayer() = default;
  ~FakeLayer() override = default;

  LinkData* RegisterInternal(hci::ConnectionHandle handle,
                             hci::Connection::Role role,
                             hci::Connection::LinkType link_type,
                             LinkErrorCallback link_error_callback,
                             async_dispatcher_t* dispatcher);
  fbl::RefPtr<Channel> OpenFakeChannel(LinkData* link, ChannelId id,
                                       ChannelId remote_id);
  fbl::RefPtr<Channel> OpenFakeFixedChannel(LinkData* link, ChannelId id) {
    return OpenFakeChannel(link, id, id);
  }
  LinkData& FindLinkData(hci::ConnectionHandle handle);

  bool initialized_ = false;
  std::unordered_map<hci::ConnectionHandle, LinkData> links_;
  ChannelCallback chan_cb_;
  std::unordered_map<PSM, ChannelDelivery> inbound_conn_cbs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLayer);
};

}  // namespace testing
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_FAKE_LAYER_H_
