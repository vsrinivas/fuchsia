// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/sys/service/cpp/service_aggregate.h>
#include <lib/sys/service/cpp/service_watcher.h>

namespace sys {

zx_status_t ServiceWatcher::Begin(const ServiceAggregateBase& service_aggregate,
                                  async_dispatcher_t* dispatcher) {
  fidl::SynchronousInterfacePtr<fuchsia::io::DirectoryWatcher> watcher;
  zx_status_t fidl_status;
  zx_status_t status = service_aggregate.proxy()->Watch(fuchsia::io::WatchMask::EXISTING |
                                                            fuchsia::io::WatchMask::ADDED |
                                                            fuchsia::io::WatchMask::REMOVED,
                                                        0, watcher.NewRequest(), &fidl_status);
  if (status != ZX_OK) {
    return status;
  }
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }

  buf_.resize(fuchsia::io::MAX_BUF);
  client_end_ = watcher.Unbind().TakeChannel();
  wait_.set_object(client_end_.get());
  wait_.set_trigger(ZX_CHANNEL_READABLE);
  return wait_.Begin(dispatcher);
}

zx_status_t ServiceWatcher::Cancel() { return wait_.Cancel(); }

void ServiceWatcher::OnWatchedEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                    zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK || !(signal->observed & ZX_CHANNEL_READABLE)) {
    return;
  }
  uint32_t size = buf_.size();
  status = client_end_.read(0, buf_.data(), nullptr, size, 0, &size, nullptr);
  if (status != ZX_OK) {
    return;
  }

  for (auto i = buf_.begin(), end = buf_.begin() + size; std::distance(i, end) > 2;) {
    // Process message structure, as described by fuchsia::io::WatchedEvent.
    fuchsia::io::WatchEvent event = static_cast<fuchsia::io::WatchEvent>(*i++);
    uint8_t len = *i++;
    // Restrict the length to the remaining size of the buffer.
    len = std::min<uint8_t>(len, std::max(0l, std::distance(i, end)));
    // If the entry is valid, invoke the callback.
    if (len != 1 || *i != '.') {
      std::string instance(reinterpret_cast<char*>(i.base()), len);
      callback_(event, std::move(instance));
    }
    i += len;
  }

  wait_.Begin(dispatcher);
}

}  // namespace sys
