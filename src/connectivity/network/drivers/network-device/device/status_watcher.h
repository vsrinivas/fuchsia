// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_STATUS_WATCHER_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_STATUS_WATCHER_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/sync/completion.h>
#include <threads.h>

#include <queue>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "definitions.h"

namespace network::internal {

template <typename F>
void WithWireStatus(F fn, port_status_t status) {
  netdev::wire::PortStatus::Frame_ frame;
  netdev::wire::PortStatus wire_status(
      fidl::ObjectView<netdev::wire::PortStatus::Frame_>::FromExternal(&frame));
  wire_status.set_flags(netdev::wire::StatusFlags::TruncatingUnknown(status.flags));
  wire_status.set_mtu(status.mtu);

  fn(wire_status);
}

class StatusWatcher : public fbl::DoublyLinkedListable<std::unique_ptr<StatusWatcher>>,
                      public fidl::WireServer<netdev::StatusWatcher> {
 public:
  explicit StatusWatcher(uint32_t max_queue);
  ~StatusWatcher() override;

  zx_status_t Bind(async_dispatcher_t* dispatcher, fidl::ServerEnd<netdev::StatusWatcher> channel,
                   fit::callback<void(StatusWatcher*)> closed_callback);
  void Unbind();

  void PushStatus(const port_status_t& status);

 private:
  void WatchStatus(WatchStatusRequestView request, WatchStatusCompleter::Sync& _completer) override;

  fbl::Mutex lock_;
  uint32_t max_queue_;
  std::optional<port_status_t> last_observed_ __TA_GUARDED(lock_);
  std::queue<port_status_t> queue_ __TA_GUARDED(lock_);
  std::optional<WatchStatusCompleter::Async> pending_txn_ __TA_GUARDED(lock_);
  std::optional<fidl::ServerBindingRef<netdev::StatusWatcher>> binding_ __TA_GUARDED(lock_);
  fit::callback<void(StatusWatcher*)> closed_cb_;
};

using StatusWatcherList = fbl::DoublyLinkedList<std::unique_ptr<StatusWatcher>>;

}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_STATUS_WATCHER_H_
