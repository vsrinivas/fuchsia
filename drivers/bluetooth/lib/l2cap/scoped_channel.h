// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SCOPED_CHANNEL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SCOPED_CHANNEL_H_

#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace l2cap {

// A Channel wrapper that automatically deactivates a channel when it gets
// deleted.
class ScopedChannel final {
 public:
  explicit ScopedChannel(fbl::RefPtr<Channel> channel);
  ScopedChannel() = default;
  ~ScopedChannel();

  // Resets the underlying channel to the one that is provided. Any previous
  // channel will be deactivated.
  void Reset(fbl::RefPtr<Channel> new_channel);

  inline void operator=(decltype(nullptr)) { Close(); }
  inline operator bool() const { return static_cast<bool>(chan_); }

  inline Channel* get() const { return chan_.get(); }
  inline Channel* operator->() const { return get(); }

 private:
  void Close();

  fbl::RefPtr<Channel> chan_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedChannel);
};

}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SCOPED_CHANNEL_H_
