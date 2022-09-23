// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_APP_
#define SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_APP_

#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <fbl/macros.h>

namespace bt_beacon_reader {

class App final {
 public:
  App(async::Loop* loop, bool just_tilts);

  void StartScanning();

 private:
  void Watch();
  void OnPeerDiscovered(const fuchsia::bluetooth::le::Peer& peer) const;

  async::Loop* const loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::bluetooth::le::CentralPtr central_;
  fuchsia::bluetooth::le::ScanResultWatcherPtr result_watcher_;

  bool just_tilts_;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(App);
};

}  // namespace bt_beacon_reader

#endif /* SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_APP_ */
