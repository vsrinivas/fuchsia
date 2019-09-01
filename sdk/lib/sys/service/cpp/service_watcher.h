// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_SERVICE_CPP_SERVICE_WATCHER_H_
#define LIB_SYS_SERVICE_CPP_SERVICE_WATCHER_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include <vector>

namespace sys {

class ServiceAggregateBase;

// A watcher for service instances.
//
// Watching is automatically stopped on destruction.
class ServiceWatcher final {
 public:
  // A callback to be invoked when service instances are added or removed.
  //
  // |event| will be either fuchsia::io::WATCH_MASK_EXISTING, if an instance was
  // existing at the beginning, fuchsia::io::WATCH_EVENT_ADDED, if an instance
  // was added, or fuchsia::io::WATCH_EVENT_REMOVED, if an instance was removed.
  // |instance| will be the name of the instance associated with the event.
  using Callback = fit::function<void(uint8_t event, std::string instance)>;

  // Constructs a watcher for service instances.
  //
  // Each time a service instance is added or removed, |callback| is invoked.
  explicit ServiceWatcher(Callback callback) : callback_(std::move(callback)) {}

  // Begins watching for service instances in a service directory.
  zx_status_t Begin(const ServiceAggregateBase& service_aggregate, async_dispatcher_t* dispatcher);

  // Cancels watching for service instances.
  zx_status_t Cancel();

 private:
  void OnWatchedEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal);

  Callback callback_;
  std::vector<uint8_t> buf_;
  zx::channel client_end_;
  async::WaitMethod<ServiceWatcher, &ServiceWatcher::OnWatchedEvent> wait_{this};
};

}  // namespace sys

#endif  // LIB_SYS_SERVICE_CPP_SERVICE_WATCHER_H_
