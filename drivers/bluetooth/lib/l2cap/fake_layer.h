// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

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

  void Initialize() override;
  void ShutDown() override;

 protected:
  void RegisterLE(hci::ConnectionHandle handle,
                  hci::Connection::Role role,
                  const LEConnectionParameterUpdateCallback& callback,
                  fxl::RefPtr<fxl::TaskRunner> task_runner) override;
  void Unregister(hci::ConnectionHandle handle) override;
  void OpenFixedChannel(hci::ConnectionHandle handle,
                        ChannelId id,
                        ChannelCallback callback,
                        fxl::RefPtr<fxl::TaskRunner> callback_runner) override;

 private:
  friend class fbl::RefPtr<FakeLayer>;
  FakeLayer() = default;
  ~FakeLayer() override = default;

  struct LinkData {
    hci::ConnectionHandle handle;
    hci::Connection::Role role;
    hci::Connection::LinkType type;

    // LE-only
    LEConnectionParameterUpdateCallback le_conn_param_cb;
    fxl::RefPtr<fxl::TaskRunner> le_conn_param_runner;
  };

  bool initialized_ = false;
  std::unordered_map<hci::ConnectionHandle, LinkData> links_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLayer);
};

}  // namespace testing
}  // namespace l2cap
}  // namespace btlib
