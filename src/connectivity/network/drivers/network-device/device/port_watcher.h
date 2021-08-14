// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PORT_WATCHER_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PORT_WATCHER_H_

#include <fuchsia/hardware/network/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/server.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "definitions.h"

namespace network::internal {

class PortWatcher : public fbl::DoublyLinkedListable<std::unique_ptr<PortWatcher>>,
                    public fidl::WireServer<netdev::PortWatcher> {
 public:
  // Maximum number of port events that can be queued before the channel is closed.
  static constexpr size_t kMaximumQueuedEvents = MAX_PORTS * 2;
  using List = fbl::SizedDoublyLinkedList<std::unique_ptr<PortWatcher>>;
  using ClosedCallback = fit::callback<void(PortWatcher&)>;

  // Binds the watcher to |dispatcher| serving on |channel|.
  // |existing_ports| contains the port identifiers to be included in the watcher's existing ports
  // list.
  // |closed_callback| is called when the watcher is closed by the peer or by a call to |Unbind|.
  zx_status_t Bind(async_dispatcher_t* dispatcher, fbl::Span<const uint8_t> existing_ports,
                   fidl::ServerEnd<netdev::PortWatcher> channel, ClosedCallback closed_callback);
  // Unbinds the port watcher if currently bound.
  void Unbind();

  // Notifies peer of port addition.
  void PortAdded(uint8_t port_id);
  // Notifies peer of port removal.
  void PortRemoved(uint8_t port_id);

  // FIDL protocol implementation.
  void Watch(WatchRequestView request, WatchCompleter::Sync& completer) override;

 private:
  // Helper class to provide owned FIDL port event union and intrusive linked list.
  class Event : public fbl::DoublyLinkedListable<std::unique_ptr<Event>> {
   public:
    Event() = default;
    Event(Event&& other) = delete;
    Event(const Event& other);

    void SetExisting(uint8_t port_id);
    void SetAdded(uint8_t port_id);
    void SetRemoved(uint8_t port_id);
    void SetIdle();

    netdev::wire::DevicePortEvent event() const { return event_; }

   private:
    netdev::wire::DevicePortEvent event_;
    netdev::wire::Empty empty_;
    uint8_t port_id_;
  };

  // Queues an event in the internal queue.
  // Returns |ZX_ERR_NO_MEMORY| if it can't allocate queue space.
  // Returns |ZX_ERR_CANCELED| if too many events are already enqueued.
  [[nodiscard]] zx_status_t QueueEvent(const Event& event) __TA_REQUIRES(lock_);
  // Processes a single event, firing a pending FIDL response if one exists or enqueuing it for
  // later consumption.
  //
  // Closes the channel with an epitaph and unbinds on queueing errors.
  void ProcessEvent(const Event& event) __TA_REQUIRES(lock_);

  fbl::Mutex lock_;
  ClosedCallback closed_cb_;
  std::optional<WatchCompleter ::Async> pending_txn_ __TA_GUARDED(lock_);
  fbl::DoublyLinkedList<std::unique_ptr<Event>, fbl::DefaultObjectTag, fbl::SizeOrder::Constant>
      event_queue_ __TA_GUARDED(lock_);
  std::optional<fidl::ServerBindingRef<netdev::PortWatcher>> binding_ __TA_GUARDED(lock_);
};

}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PORT_WATCHER_H_
